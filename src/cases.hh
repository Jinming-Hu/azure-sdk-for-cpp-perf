#pragma once

#include <chrono>
#include <cstdint>

#include "transport.hh"

struct transfer_configuration
{
  int64_t blob_size;
  int num_blobs;
  int concurrency;
};

struct transfer_result
{
  std::chrono::milliseconds total_time_ms;
  bool exception_observed = false;
};

struct case_base
{
  const std::string name;
  case_base(std::string name) : name(std::move(name)) {}
  virtual transfer_result operator()(transport& transport, transfer_configuration& transfer_config)
      = 0;
  virtual ~case_base() {}
};

struct case_download : case_base
{
  case_download() : case_base("download") {}

  transfer_result operator()(transport& transport, transfer_configuration& transfer_config)
      override;
};

struct case_upload : case_base
{
  case_upload() : case_base("upload") {}

  transfer_result operator()(transport& transport, transfer_configuration& transfer_config)
      override;
};