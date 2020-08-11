#include "track1_test.h"

#include <cassert>
#include <atomic>

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
    client.upload_block_blob_from_buffer(
        containerName, blobName, blobContent.data(), {}, blobContent.length(), 1);
  }

  std::atomic<int> counter = numBlobs;
  auto threadFunc = [&]() {
    while (counter.fetch_sub(1) > 0)
    {
      omstream os(&blobContent[0], blobContent.size());
      auto ret = client.download_blob_to_stream(containerName, blobName, 0, blobSize, os).get();
      assert(ret.success());
    }
  };

  auto start = std::chrono::steady_clock::now();
  std::vector<std::thread> ths;
  for (int i = 0; i < concurrency; ++i)
  {
    ths.emplace_back(threadFunc);
  }
  for (auto& th : ths)
  {
    th.join();
  }
  auto end = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
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

  std::atomic<int> counter = numBlobs;
  auto threadFunc = [&]() {
    while (counter.fetch_sub(1) > 0)
    {
      imstream is(&blobContent[0], blobContent.size());
      auto ret = client.upload_block_blob_from_stream(containerName, blobName, is, {}).get();
      assert(ret.success());
    }
  };

  auto start = std::chrono::steady_clock::now();
  std::vector<std::thread> ths;
  for (int i = 0; i < concurrency; ++i)
  {
    ths.emplace_back(threadFunc);
  }
  for (auto& th : ths)
  {
    th.join();
  }
  auto end = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}