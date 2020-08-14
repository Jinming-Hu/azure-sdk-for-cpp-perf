#include "track2_test.h"

#include <atomic>
#include <cassert>
#include <thread>

#include "blobs/blob.hpp"

#include "credential.h"

int track2_test_download(int64_t blobSize, int numBlobs, int concurrency)
{
  using namespace Azure::Storage;
  using namespace Azure::Storage::Blobs;

  auto cred = std::make_shared<SharedKeyCredential>(accountName, accountKey);

  std::string blobName = blobNamePrefix + std::to_string(blobSize);

  auto client = BlockBlobClient(
      std::string("https://") + accountName + ".blob.core.windows.net/" + containerName + "/"
          + blobName,
      cred);

  std::string blobContent;
  blobContent.resize(blobSize);
  FillBuffer(&blobContent[0], blobContent.size());

  try
  {
    auto getPropertiesResult = client.GetProperties();
    if (getPropertiesResult->ContentLength != blobSize)
    {
      throw std::runtime_error("");
    }
  }
  catch (std::runtime_error&)
  {
    client.UploadFromBuffer(
        reinterpret_cast<const uint8_t*>(blobContent.data()), blobContent.length());
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
      DownloadBlobToBufferOptions options;
      options.InitialChunkSize = blobSize;
      options.ChunkSize = blobSize;
      options.Concurrency = 1;
      client.DownloadToBuffer(
          reinterpret_cast<uint8_t*>(&blobContent[0]), blobContent.size(), options);
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

int track2_test_upload(int64_t blobSize, int numBlobs, int concurrency)
{
  using namespace Azure::Storage;
  using namespace Azure::Storage::Blobs;

  auto cred = std::make_shared<SharedKeyCredential>(accountName, accountKey);

  std::string blobName = blobNamePrefix + std::to_string(blobSize);
  auto containerClient = BlobContainerClient(
      std::string("https://") + accountName + ".blob.core.windows.net/" + containerName, cred);

  std::string blobContent;
  blobContent.resize(blobSize);
  FillBuffer(&blobContent[0], blobContent.size());

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
      auto client = containerClient.GetBlockBlobClient(blobName + "-" + std::to_string(i));

      UploadBlobOptions options;
      options.ChunkSize = blobSize;
      options.Concurrency = 1;
      client.UploadFromBuffer(
          reinterpret_cast<const uint8_t*>(blobContent.data()), blobContent.size(), options);
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

int track2_test_blocks_download(int64_t blockSize, int numBlocks, int concurrency)
{
  using namespace Azure::Storage;
  using namespace Azure::Storage::Blobs;

  auto cred = std::make_shared<SharedKeyCredential>(accountName, accountKey);

  std::string blobName
      = blobNamePrefix + std::to_string(blockSize) + "*" + std::to_string(numBlocks);

  auto client = BlockBlobClient(
      std::string("https://") + accountName + ".blob.core.windows.net/" + containerName + "/"
          + blobName,
      cred);

  std::string blobContent;
  blobContent.resize(blockSize * numBlocks);
  FillBuffer(&blobContent[0], blobContent.size());

  try
  {
    auto getPropertiesResult = client.GetProperties();
    if (getPropertiesResult->ContentLength != blockSize * numBlocks)
    {
      throw std::runtime_error("");
    }
  }
  catch (std::runtime_error&)
  {
    client.UploadFromBuffer(
        reinterpret_cast<const uint8_t*>(blobContent.data()), blobContent.length());
  }

  DownloadBlobToBufferOptions options;
  options.InitialChunkSize = blockSize;
  options.ChunkSize = blockSize;
  options.Concurrency = concurrency;
  auto start = std::chrono::steady_clock::now();
  client.DownloadToBuffer(reinterpret_cast<uint8_t*>(&blobContent[0]), blobContent.size(), options);
  auto end = std::chrono::steady_clock::now();
  int ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  return ms;
}

int track2_test_blocks_upload(int64_t blockSize, int numBlocks, int concurrency)
{
  using namespace Azure::Storage;
  using namespace Azure::Storage::Blobs;

  auto cred = std::make_shared<SharedKeyCredential>(accountName, accountKey);

  std::string blobName
      = blobNamePrefix + std::to_string(blockSize) + "*" + std::to_string(numBlocks);

  auto client = BlockBlobClient(
      std::string("https://") + accountName + ".blob.core.windows.net/" + containerName + "/"
          + blobName,
      cred);

  std::string blobContent;
  blobContent.resize(blockSize * numBlocks);
  FillBuffer(&blobContent[0], blobContent.size());

  UploadBlobOptions options;
  options.ChunkSize = blockSize;
  options.Concurrency = concurrency;
  auto start = std::chrono::steady_clock::now();
  client.UploadFromBuffer(
      reinterpret_cast<const uint8_t*>(blobContent.data()), blobContent.size(), options);
  auto end = std::chrono::steady_clock::now();
  int ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  return ms;
}
