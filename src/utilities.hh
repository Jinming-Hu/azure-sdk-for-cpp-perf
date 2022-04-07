#pragma once

#include <cstdint>
#include <string>

#undef SPDLOG_FMT_EXTERNAL
#include <spdlog/spdlog.h>

constexpr inline unsigned long long operator""_KB(unsigned long long x) { return x * 1024; }
constexpr inline unsigned long long operator""_MB(unsigned long long x) { return x * 1024 * 1024; }
constexpr inline unsigned long long operator""_GB(unsigned long long x)
{
  return x * 1024 * 1024 * 1024;
}

void fill_buffer(uint8_t* buffer, size_t size);

void check_build_environment();
bool is_connection_string_valid(const std::string& connection_string);
void validate_azure_vm();
std::string get_account_name_from_connection_string();
std::string get_access_key_from_connection_string();

std::string get_blob_name(size_t blob_size, int index = 0);
void init_blobs(size_t blob_size, int num_blobs);

struct libcurl_raii
{
  libcurl_raii();
  ~libcurl_raii();
};

struct logger_raii
{
  logger_raii();
  ~logger_raii();
  bool should_flush = false;

private:
  std::string m_log_filename;
};
