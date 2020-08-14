#include "credential.h"
#include "track1_test.h"
#include "track2_test.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <iostream>
#include <random>
#include <string>
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

std::string SizeToString(std::size_t s)
{
  std::string u = "byte";
  if (s % 1_GB == 0)
  {
    u = "GiB";
    s /= 1_GB;
  }
  else if (s % 1_MB == 0)
  {
    u = "MiB";
    s /= 1_MB;
  }
  else if (s % 1_KB == 0)
  {
    u = "KiB";
    s /= 1_KB;
  }
  return std::to_string(s) + u;
}

int main(int argc, char** argv)
{
  std::vector<TestConf> confs
      = {{5, 5000, 32}, {10_KB, 5000, 32}, {10_MB, 1000, 32}, {1_GB, 16, 8}, {1_GB, 64, 32}};

  std::vector<Suite> suites = {
      {"Track1 Upload", track1_test_upload},
      {"Track1 Download", track1_test_download},
      {"Track2 Upload", track2_test_upload},
      {"Track2 Download", track2_test_download},
  };

  int ss = 0;
  int se = suites.size();
  int cs = 0;
  int ce = confs.size();
  if (argc >= 2)
  {
    ss = std::stoi(argv[1]);
    se = ss + 1;
  }
  if (argc >= 3)
  {
    cs = std::stoi(argv[2]);
    ce = cs + 1;
  }

  if (argc == 1)
  {
    std::random_device rd{};
    auto rng = std::default_random_engine{rd()};
    std::shuffle(suites.begin(), suites.end(), rng);
  }

  for (int i = ss; i < se; ++i)
  {
    const Suite& s = suites[i];
    printf("%s:\n", s.name.data());

    for (int j = cs; j < ce; ++j)
    {
      const TestConf& conf = confs[j];
      printf(
          "    Transfer %d %s blobs with %d threads\n",
          conf.numBlobs,
          SizeToString(conf.blobSize).data(),
          conf.concurrency);

      int ms = s.func(conf.blobSize, conf.numBlobs, conf.concurrency);

      double mbps = static_cast<double>(conf.numBlobs * conf.blobSize) / 1_MB / ms * 1000;
      double tps = static_cast<double>(conf.numBlobs) / ms * 1000;

      printf("    %d ms, %lf MiB/s, %lf op/s\n", ms, mbps, tps);
    }
  }

  TestConf blocksConf = {10_MB, 1000, 32};

  std::vector<Suite> blocksSuites = {
      {"Track2 Blocks Upload", track2_test_blocks_upload},
      {"Track2 Blocks Download", track2_test_blocks_download},
  };

  if (argc == 1)
  {
    for (const auto& suite : blocksSuites)
    {
      printf("%s:\n", suite.name.data());
      printf(
          "    Transfer %d %s blocks with %d threads\n",
          blocksConf.numBlobs,
          SizeToString(blocksConf.blobSize).data(),
          blocksConf.concurrency);

      int ms = suite.func(blocksConf.blobSize, blocksConf.numBlobs, blocksConf.concurrency);
      double mbps
          = static_cast<double>(blocksConf.numBlobs * blocksConf.blobSize) / 1_MB / ms * 1000;
      double tps = static_cast<double>(blocksConf.numBlobs) / ms * 1000;
      printf("    %d ms, %lf MiB/s, %lf op/s\n", ms, mbps, tps);
    }
  }
  return 0;
}
