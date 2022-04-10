#include "utilities.hh"

#ifdef _WIN32
#include <intrin.h>
#endif

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
#include <mutex>
#include <numeric>
#include <set>
#include <thread>
#include <type_traits>
#include <vector>

#include <azure/core/http/curl_transport.hpp>
#include <azure/core/http/http.hpp>
#include <azure/core/internal/cryptography/sha_hash.hpp>
#include <azure/storage/blobs.hpp>
#include <azure/storage/common/storage_credential.hpp>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/base_sink.h>

#include "constants.hh"

namespace {

uint64_t rand_int(uint64_t offset)
{
#ifdef _WIN32
  uint64_t high_part = 0x12e15e35b500f16e;
  uint64_t low_part = 0x2e714eb2b37916a5;
  return __umulh(low_part, offset) + high_part * offset;
#else
  using uint128_t = __uint128_t;
  constexpr uint128_t mult = static_cast<uint128_t>(0x12e15e35b500f16e) << 64 | 0x2e714eb2b37916a5;
  uint128_t product = offset * mult;
  return product >> 64;
#endif
}
} // namespace

void fill_buffer(uint8_t* buffer, size_t size)
{
  constexpr size_t int_size = sizeof(uint64_t);

  while (size >= int_size)
  {
    *(reinterpret_cast<uint64_t*>(buffer)) = rand_int(size);
    size -= int_size;
    buffer += int_size;
  }
  if (size)
  {
    uint64_t r = rand_int(size);
    std::memcpy(buffer, &r, size);
  }
}

void check_build_environment()
{
  spdlog::info("OS: {}", BUILD_OS_VERSION);
  spdlog::info("compiler: {}", BUILD_COMPILER_VERSION);
  spdlog::info("azure-core-cpp version: {}", AZURE_CORE_GIT_VERSION);
  spdlog::info("azure-storage-common-cpp version: {}", AZURE_STORAGE_COMMON_GIT_VERSION);
  spdlog::info("azure-storage-blobs-cpp version: {}", AZURE_STORAGE_BLOBS_GIT_VERSION);
}

bool is_connection_string_valid(const std::string& str)
{
  if (std::string(str).empty())
  {
    return false;
  }
  auto blob_service_client
      = Azure::Storage::Blobs::BlobServiceClient::CreateFromConnectionString(str);
  try
  {
    blob_service_client.GetAccountInfo();
  }
  catch (std::exception&)
  {
    return false;
  }
  return true;
}

void validate_azure_vm()
{
  try
  {
    auto request = Azure::Core::Http::Request(
        Azure::Core::Http::HttpMethod::Get,
        Azure::Core::Url("http://169.254.169.254/metadata/instance?api-version=2021-02-01"));
    request.SetHeader("Metadata", "true");
    Azure::Core::Http::CurlTransportOptions transport_options;
    transport_options.Proxy = "";
    Azure::Core::Http::CurlTransport curl_transport(transport_options);
    auto response = curl_transport.Send(request, Azure::Core::Context::ApplicationContext);
    auto response_body_binary = response->GetBody();
    if (response_body_binary.empty())
    {
      response_body_binary = response->ExtractBodyStream()->ReadToEnd();
    }
    std::string json_body(response_body_binary.begin(), response_body_binary.end());
    if (json_body.empty())
    {
      throw std::runtime_error(
          "failed to get response from Azure Instance Metadata Service, "
          + std::to_string(
              static_cast<typename std::underlying_type<Azure::Core::Http::HttpStatusCode>::type>(
                  response->GetStatusCode()))
          + " " + response->GetReasonPhrase());
    }
    auto json_object = nlohmann::json::parse(json_body);
    std::string resource_id = json_object["compute"]["resourceId"];
    spdlog::info("Azure VM resource ID: {}", resource_id);
  }
  catch (std::exception& e)
  {
    spdlog::error("failed to detect Azure VM resource ID");
    spdlog::error(e.what());
  }
}

std::string get_account_name_from_connection_string()
{
  return Azure::Storage::_internal::ParseConnectionString(connection_string).AccountName;
}

std::string get_access_key_from_connection_string()
{
  return Azure::Storage::_internal::ParseConnectionString(connection_string).AccountKey;
}

std::string get_blob_name(size_t blob_size, int index)
{
  return "blob-" + std::to_string(blob_size) + "-" + std::to_string(index);
}

void init_blobs(size_t blob_size, int num_blobs)
{
  using namespace Azure::Storage::Blobs;

  BlobClientOptions clientOptions;
  clientOptions.Transport.Transport = std::make_shared<Azure::Core::Http::CurlTransport>();
  auto container_client = BlobContainerClient::CreateFromConnectionString(
      connection_string, container_name, clientOptions);
  {
    static std::once_flag flag;
    std::call_once(flag, [container_client]() { container_client.CreateIfNotExists(); });
  }

  std::vector<uint8_t> blob_content(blob_size);
  fill_buffer(blob_content.data(), blob_content.size());

  std::mutex m;
  static std::set<std::string> blob_name_set;

  std::vector<int> indices;
  for (int i = 0; i < num_blobs; ++i)
  {
    const std::string blob_name = get_blob_name(blob_size, i);
    if (blob_name_set.count(blob_name) == 0)
    {
      indices.push_back(i);
    }
  }

  std::atomic<int> counter(static_cast<int>(indices.size()));
  std::vector<std::thread> ths;
  auto thread_func = [&]() {
    while (true)
    {
      int i = counter.fetch_sub(1) - 1;
      if (i < 0)
      {
        break;
      }
      i = indices[i];
      const std::string blob_name = get_blob_name(blob_size, i);
      auto blob_client = container_client.GetBlockBlobClient(blob_name);
      try
      {
        blob_client.UploadFrom(blob_content.data(), blob_content.size());
      }
      catch (std::exception&)
      {
        spdlog::error("failed when initialising test resource");
        throw;
      }
      {
        std::lock_guard<std::mutex> g(m);
        blob_name_set.insert(blob_name);
      }
    }
  };
  for (int i = 0; i < 32; ++i)
  {
    ths.emplace_back(thread_func);
  }
  for (auto& th : ths)
  {
    th.join();
  }
}

libcurl_raii::libcurl_raii() { curl_global_init(CURL_GLOBAL_DEFAULT); }
libcurl_raii::~libcurl_raii() { curl_global_cleanup(); }

template <class Mutex> class azure_storage_sink : public spdlog::sinks::base_sink<Mutex> {
protected:
  void sink_it_(const spdlog::details::log_msg& msg) override
  {
    spdlog::memory_buf_t formatted;
    spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
    m_buffer += fmt::to_string(formatted);
  }
  void flush_() override {}

private:
  std::string m_buffer;

  friend struct logger_raii;
};
using azure_storage_sink_mt = azure_storage_sink<std::mutex>;

logger_raii::logger_raii()
{
  if (!is_connection_string_valid(log_connection_string))
  {
    spdlog::warn(
        "failed to validate log connection string, log won't be uploaded to azure storage");
  }
  else
  {
    const std::string timestamp = Azure::DateTime(std::chrono::system_clock::now())
                                      .ToString(
                                          Azure::DateTime::DateFormat::Rfc3339,
                                          Azure::DateTime::TimeFractionFormat::Truncate);
    const std::vector<uint8_t> hash = Azure::Core::Cryptography::_internal::Sha256Hash().Final(
        reinterpret_cast<const uint8_t*>(timestamp.data()), timestamp.length());
    const std::string hash_hex = std::accumulate(
        hash.begin(), hash.end(), std::string(), [](const std::string& lhs, uint8_t rhs) {
          static const char t[16]
              = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
          return lhs + t[rhs >> 4] + t[rhs & 0x0f];
        });
    m_log_filename = timestamp + "-" + hash_hex.substr(0, 7) + ".log";

    auto azure_storage_sink = std::make_shared<azure_storage_sink_mt>();
    spdlog::default_logger()->sinks().push_back(azure_storage_sink);
  }
  spdlog::default_logger()->set_pattern("%+", spdlog::pattern_time_type::utc);
}

logger_raii::~logger_raii()
{
  using namespace Azure::Storage::Blobs;

  if (m_log_filename.empty())
  {
    return;
  }
  auto azure_storage_sink
      = std::dynamic_pointer_cast<azure_storage_sink_mt>(spdlog::default_logger()->sinks().back());
  spdlog::default_logger()->sinks().pop_back();

  if (!should_flush)
  {
    return;
  }

  auto blob_container_client
      = BlobContainerClient::CreateFromConnectionString(log_connection_string, log_container_name);
  try
  {
    blob_container_client.CreateIfNotExists();
    UploadBlockBlobFromOptions options;
    options.HttpHeaders.ContentType = "text/plain";
    blob_container_client.GetBlockBlobClient(m_log_filename)
        .UploadFrom(
            reinterpret_cast<const uint8_t*>(azure_storage_sink->m_buffer.data()),
            azure_storage_sink->m_buffer.length(),
            options);
  }
  catch (std::exception& e)
  {
    spdlog::error("failed to upload log to azure storage");
    spdlog::error(e.what());
    return;
  }
  azure_storage_sink->m_buffer.clear();
  spdlog::info("log has been uploaded to azure storage {}/{}", log_container_name, m_log_filename);
}