#pragma once

#ifdef _WIN32
#include <intrin.h>
#endif

#include <cstddef>
#include <cstdint>

constexpr static const char* accountName = "";
constexpr static const char* accountKey = "";
constexpr static const char* containerName = "perf-test";
constexpr static const char* blobNamePrefix = "perf-test-";

inline uint64_t RandInt(uint64_t offset)
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

inline void FillBuffer(char* buffer, std::size_t size)
{
  constexpr std::size_t intSize = sizeof(uint64_t);

  while (size >= intSize)
  {
    *(reinterpret_cast<uint64_t*>(buffer)) = RandInt(size);
    size -= intSize;
    buffer += intSize;
  }
}
