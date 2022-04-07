#pragma once

#include "cases.hh"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "constants.hh"
#include "utilities.hh"

transfer_result case_download::operator()(
    transport& transport,
    transfer_configuration& transfer_config)
{
  transport.reset(transfer_config.concurrency);

  const std::string blob_name = get_blob_name(transfer_config.blob_size);
  init_blobs(transfer_config.blob_size, 1);

  std::vector<std::vector<uint8_t>> buffer_array(
      transfer_config.concurrency, std::vector<uint8_t>(transfer_config.blob_size, uint8_t(0)));

  std::atomic<int> counter(transfer_config.num_blobs);
  std::atomic<bool> exception_observed(false);
  std::mutex lock;
  std::chrono::microseconds total_time_us(0);
  auto thread_func = [&](int thread_id) {
    auto start = std::chrono::steady_clock::now();
    while (true)
    {
      int i = counter.fetch_sub(1);
      if (i <= 0)
      {
        break;
      }
      try
      {
        transport.download_blob(
            blob_name, buffer_array[thread_id].data(), buffer_array[thread_id].size());
      }
      catch (std::exception& e)
      {
        exception_observed.store(true, std::memory_order_relaxed);
        spdlog::debug(e.what());
        break;
      }
    }
    auto end = std::chrono::steady_clock::now();
    {
      std::lock_guard<std::mutex> guard(lock);
      total_time_us += std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    }
  };

  std::vector<std::thread> ths;
  for (int i = 0; i < transfer_config.concurrency; ++i)
  {
    ths.emplace_back(thread_func, i);
  }
  for (auto& th : ths)
  {
    th.join();
  }

  transfer_result ret;
  ret.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      total_time_us / transfer_config.concurrency);
  ret.exception_observed = exception_observed;
  return ret;
}

transfer_result case_upload::operator()(
    transport& transport,
    transfer_configuration& transfer_config)
{
  transport.reset(transfer_config.concurrency);

  std::vector<uint8_t> buffer(transfer_config.blob_size);
  fill_buffer(buffer.data(), buffer.size());

  std::atomic<int> counter(transfer_config.num_blobs);
  std::atomic<bool> exception_observed(false);
  std::mutex lock;
  std::chrono::microseconds total_time_us(0);
  auto thread_func = [&]() {
    auto start = std::chrono::steady_clock::now();
    while (true)
    {
      int i = counter.fetch_sub(1);
      if (i <= 0)
      {
        break;
      }
      try
      {
        std::string blob_name = get_blob_name(transfer_config.blob_size, i);
        transport.upload_blob(blob_name, buffer.data(), buffer.size());
      }
      catch (std::exception& e)
      {
        exception_observed.store(true, std::memory_order_relaxed);
        spdlog::debug(e.what());
        break;
      }
    }
    auto end = std::chrono::steady_clock::now();
    {
      std::lock_guard<std::mutex> guard(lock);
      total_time_us += std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    }
  };

  std::vector<std::thread> ths;
  for (int i = 0; i < transfer_config.concurrency; ++i)
  {
    ths.emplace_back(thread_func);
  }
  for (auto& th : ths)
  {
    th.join();
  }

  transfer_result ret;
  ret.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      total_time_us / transfer_config.concurrency);
  ret.exception_observed = exception_observed;
  return ret;
}