#include <algorithm>
#include <chrono>
#include <memory>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

#include "cases.hh"
#include "constants.hh"
#include "transport.hh"
#include "utilities.hh"

struct benchmark_case
{
  transfer_configuration transfer_config;
  std::shared_ptr<::transport> transport;
  std::shared_ptr<case_base> func;
};

void perform(const std::vector<benchmark_case>& benchmark_cases)
{
  std::vector<size_t> task_order;
  for (size_t i = 0; i < benchmark_cases.size(); ++i)
  {
    task_order.insert(task_order.end(), repeat, i);
  }
  {
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(task_order.begin(), task_order.end(), g);
  }
  for (auto i : task_order)
  {
    auto casei = benchmark_cases[i];
    while (true)
    {
      auto transfer_result = (*casei.func)(*casei.transport, casei.transfer_config);
      if (transfer_result.exception_observed)
      {
        spdlog::warn(
            "exception observed with {}, sleep {} seconds",
            casei.transport->name,
            exception_sleep_seconds);
        std::this_thread::sleep_for(std::chrono::seconds(exception_sleep_seconds));
        continue;
      }
      spdlog::info(
          "{} used {}ms to {} {} {}-byte blobs with {} threads",
          casei.transport->name,
          transfer_result.total_time_ms.count(),
          casei.func->name,
          casei.transfer_config.num_blobs,
          casei.transfer_config.blob_size,
          casei.transfer_config.concurrency);
      break;
    }
    std::this_thread::sleep_for(std::chrono::seconds(delay_seconds_between_tasks));
  }
}

int main()
{
  libcurl_raii libcurl_raii_instance;
  logger_raii logger_raii_instance;

  spdlog::info("started");

  if (!is_connection_string_valid(connection_string))
  {
    spdlog::error("invalid connection string");
    return 1;
  }
  spdlog::info("using storage account: {}", get_account_name_from_connection_string());
  check_build_environment();
  validate_azure_vm();

  std::vector<transfer_configuration> transfer_configs;
  transfer_configs.push_back({5, 10000, 32});
  transfer_configs.push_back({10_KB, 10000, 32});
  transfer_configs.push_back({10_MB, 1000, 32});
  transfer_configs.push_back({1_GB, 32, 8});
  transfer_configs.push_back({1_GB, 128, 32});

  std::vector<std::shared_ptr<transport>> transports;
  transports.push_back(std::make_shared<cpplite_transport>());
  transports.push_back(std::make_shared<track2_curl_transport>());
#if defined(_WIN32)
  transports.push_back(std::make_shared<track2_winhttp_transport>());
#endif

  std::vector<std::shared_ptr<case_base>> case_functions;
  case_functions.push_back(std::make_shared<case_download>());
  case_functions.push_back(std::make_shared<case_upload>());

  std::vector<benchmark_case> benchmark_cases;
  for (const auto& c : transfer_configs)
  {
    for (auto& t : transports)
    {
      for (auto& f : case_functions)
      {
        benchmark_cases.push_back({c, t, f});
      }
    }
  }
  for (size_t i = 0; i < transfer_configs.size(); ++i)
  {
    const auto& c = transfer_configs[i];
    spdlog::info(
        "transfer config {}: blob size: {} bytes, number of blobs: {}, concurrency: {}",
        i + 1,
        c.blob_size,
        c.num_blobs,
        c.concurrency);
  }
  spdlog::info(
      "transports: {}",
      std::accumulate(
          transports.begin(), transports.end(), std::string(), [](std::string& lhs, auto& rhs) {
            return lhs.empty() ? rhs->name : lhs + ", " + rhs->name;
          }));
  spdlog::info("baseline transport: {}", transports[0]->name);
  spdlog::info(
      "benchmark cases: {}",
      std::accumulate(
          case_functions.begin(),
          case_functions.end(),
          std::string(),
          [](std::string& lhs, auto& rhs) {
            return lhs.empty() ? rhs->name : lhs + ", " + rhs->name;
          }));
  spdlog::info("repeat times: {}", repeat);
  perform(benchmark_cases);
  spdlog::info("exited");
  logger_raii_instance.should_flush = true;

  return 0;
}
