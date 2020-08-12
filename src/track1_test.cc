#include "track1_test.h"

#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>

#include "blob/blob_client.h"
#include "mstream.h"
#include "storage_account.h"
#include "storage_credential.h"

#include "credential.h"

int track1_test_download(int64_t blobSize, int numBlobs, int concurrency)
{
  using namespace azure::storage_lite;

  auto cred = std::make_shared<shared_key_credential>(accountName, accountKey);
  auto account = std::make_shared<storage_account>(accountName, cred, /* use_https */ true);

  blob_client client(account, concurrency);

  std::string blobContent;
  blobContent.resize(blobSize);
  FillBuffer(&blobContent[0], blobContent.size());

  std::string blobName = blobNamePrefix + std::to_string(blobSize);

  auto getPropertiesResult = client.get_blob_properties(containerName, blobName).get();
  if (!getPropertiesResult.success() || getPropertiesResult.response().size != blobSize)
  {
    client.delete_blob(containerName, blobName);
    auto ret = client
                   .upload_block_blob_from_buffer(
                       containerName, blobName, blobContent.data(), {}, blobContent.length(), 1)
                   .get();
    if (!ret.success())
    {
      std::cout << ret.error().code << std::endl;
      std::cout << ret.error().code_name << std::endl;
      std::cout << ret.error().message << std::endl;
    }
  }

  std::atomic<int> counter(numBlobs);
  std::atomic<int> ms(0);
  auto threadFunc = [&]() {
    auto start = std::chrono::steady_clock::now();
    while (true)
    {
      int i = counter.fetch_sub(1);
      if (i <= 0)
      {
        break;
      }
      omstream os(&blobContent[0], blobContent.size());
      auto ret = client.download_blob_to_stream(containerName, blobName, 0, blobSize, os).get();
      if (!ret.success())
      {
        std::cout << ret.error().code << std::endl;
        std::cout << ret.error().code_name << std::endl;
        std::cout << ret.error().message << std::endl;
      }
    }
    auto end = std::chrono::steady_clock::now();
    ms.fetch_add(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
  };

  std::vector<std::thread> ths;
  for (int i = 0; i < concurrency; ++i)
  {
    ths.emplace_back(threadFunc);
  }
  for (auto& th : ths)
  {
    th.join();
  }

  return ms.load() / concurrency;
}

int track1_test_upload(int64_t blobSize, int numBlobs, int concurrency)
{
  using namespace azure::storage_lite;

  auto cred = std::make_shared<shared_key_credential>(accountName, accountKey);
  auto account = std::make_shared<storage_account>(accountName, cred, /* use_https */ true);

  blob_client client(account, concurrency);

  std::string blobContent;
  blobContent.resize(blobSize);
  FillBuffer(&blobContent[0], blobContent.size());

  std::string blobName = blobNamePrefix + std::to_string(blobSize);

  std::atomic<int> counter(numBlobs);
  std::atomic<int> ms(0);
  auto threadFunc = [&]() {
    auto start = std::chrono::steady_clock::now();
    while (true)
    {
      int i = counter.fetch_sub(1);
      if (i <= 0)
      {
        break;
      }
      imstream is(&blobContent[0], blobContent.size());
      auto ret = client
                     .upload_block_blob_from_stream(
                         containerName, blobName + "-" + std::to_string(i), is, {})
                     .get();
      if (!ret.success())
      {
        std::cout << ret.error().code << std::endl;
        std::cout << ret.error().code_name << std::endl;
        std::cout << ret.error().message << std::endl;
      }
    }
    auto end = std::chrono::steady_clock::now();
    ms.fetch_add(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
  };

  std::vector<std::thread> ths;
  for (int i = 0; i < concurrency; ++i)
  {
    ths.emplace_back(threadFunc);
  }
  for (auto& th : ths)
  {
    th.join();
  }

  return ms.load() / concurrency;
}
