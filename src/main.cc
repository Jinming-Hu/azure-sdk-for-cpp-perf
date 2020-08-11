#include "credential.h"
#include "track1_test.h"
#include "track2_test.h"

#include <chrono>
#include <cstdio>
#include <functional>
#include <iostream>
#include <vector>

struct Suite
{
  std::string name;
  std::function<int(int64_t, int, int)> func;
};

struct TestConf
{
  int64_t blobSize;
  int numBlobs;
  int concurrency;
};

constexpr inline unsigned long long operator""_KB(unsigned long long x) { return x * 1024; }
constexpr inline unsigned long long operator""_MB(unsigned long long x) { return x * 1024 * 1024; }
constexpr inline unsigned long long operator""_GB(unsigned long long x)
{
  return x * 1024 * 1024 * 1024;
}

int main()
{
  std::vector<TestConf> downloadConf
      = {{5, 50000, 32}, {10_KB, 50000, 32}, {10_MB, 50, 32}, {1_GB, 32, 8}};
  std::vector<TestConf> uploadConf
      = {{5, 50000, 32}, {10_KB, 50000, 32}, {10_MB, 50, 32}, {1_GB, 32, 8}};

  std::vector<Suite> suites = {
      {"Track1 Upload", track1_test_upload},
      {"Track1 Download", track1_test_download},
      {"Track2 Upload", track2_test_upload},
      {"Track2 Download", track2_test_download},
  };

  for (const Suite& s : suites)
  {
    printf("%s:\n", s.name.data());

    for (const TestConf& conf : downloadConf)
    {
      printf(
          "    Transfer %d %lld-byte blobs with %d threads\n",
          conf.numBlobs,
          conf.blobSize,
          conf.concurrency);


      int ms = s.func(conf.blobSize, conf.numBlobs, conf.concurrency);

      double mbps = static_cast<double>(conf.numBlobs * conf.blobSize) / 1_MB / ms * 1000;
      double tps = static_cast<double>(conf.numBlobs) / ms * 1000;

      printf("    %d ms, %lf MiB/s, %lf T/s\n", ms, mbps, tps);
    }
  }

  return 0;
}