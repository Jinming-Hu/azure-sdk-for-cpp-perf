#include "track2_test.h"

#include <atomic>
#include <cassert>
#include <thread>

#include "azure/storage/blobs.hpp"

#include "credential.h"

int track2_test_download(int64_t blobSize, int numBlobs, int concurrency)
{
  using namespace Azure::Storage;
  using namespace Azure::Storage::Blobs;

  auto cred = std::make_shared<StorageSharedKeyCredential>(accountName, accountKey);

  std::string blobName = blobNamePrefix + std::to_string(blobSize);

  BlobClientOptions clientOptions;
  clientOptions.Retry.MaxRetries = 0;
  auto client = BlockBlobClient(
      std::string("https://") + accountName + ".blob.core.windows.net/" + containerName + "/"
          + blobName,
      cred,
      clientOptions);

  std::string blobContent;
  blobContent.resize(blobSize);
  FillBuffer(&blobContent[0], blobContent.size());

  try
  {
    auto getPropertiesResult = client.GetProperties();
    if (getPropertiesResult.Value.BlobSize != blobSize)
    {
      throw std::runtime_error("");
    }
  }
  catch (std::runtime_error&)
  {
    client.UploadFrom(reinterpret_cast<const uint8_t*>(blobContent.data()), blobContent.length());
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
      DownloadBlobToOptions options;
      options.TransferOptions.InitialChunkSize = blobSize;
      options.TransferOptions.ChunkSize = blobSize;
      options.TransferOptions.Concurrency = 1;
      client.DownloadTo(reinterpret_cast<uint8_t*>(&blobContent[0]), blobContent.size(), options);
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

  auto cred = std::make_shared<StorageSharedKeyCredential>(accountName, accountKey);

  std::string blobName = blobNamePrefix + std::to_string(blobSize);
  BlobClientOptions clientOptions;
  clientOptions.Retry.MaxRetries = 0;
  auto containerClient = BlobContainerClient(
      std::string("https://") + accountName + ".blob.core.windows.net/" + containerName,
      cred,
      clientOptions);

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

      UploadBlockBlobFromOptions options;
      options.TransferOptions.ChunkSize = blobSize;
      options.TransferOptions.Concurrency = 1;
      client.UploadFrom(
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

  auto cred = std::make_shared<StorageSharedKeyCredential>(accountName, accountKey);

  std::string blobName
      = blobNamePrefix + std::to_string(blockSize) + "*" + std::to_string(numBlocks);

  BlobClientOptions clientOptions;
  clientOptions.Retry.MaxRetries = 0;
  auto client = BlockBlobClient(
      std::string("https://") + accountName + ".blob.core.windows.net/" + containerName + "/"
          + blobName,
      cred,
      clientOptions);

  std::string blobContent;
  blobContent.resize(blockSize * numBlocks);
  FillBuffer(&blobContent[0], blobContent.size());

  try
  {
    auto getPropertiesResult = client.GetProperties();
    if (getPropertiesResult.Value.BlobSize != blockSize * numBlocks)
    {
      throw std::runtime_error("");
    }
  }
  catch (std::runtime_error&)
  {
    client.UploadFrom(reinterpret_cast<const uint8_t*>(blobContent.data()), blobContent.length());
  }

  DownloadBlobToOptions options;
  options.TransferOptions.InitialChunkSize = blockSize;
  options.TransferOptions.ChunkSize = blockSize;
  options.TransferOptions.Concurrency = concurrency;
  auto start = std::chrono::steady_clock::now();
  client.DownloadTo(reinterpret_cast<uint8_t*>(&blobContent[0]), blobContent.size(), options);
  auto end = std::chrono::steady_clock::now();
  int ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  return ms;
}

int track2_test_blocks_upload(int64_t blockSize, int numBlocks, int concurrency)
{
  using namespace Azure::Storage;
  using namespace Azure::Storage::Blobs;

  auto cred = std::make_shared<StorageSharedKeyCredential>(accountName, accountKey);

  std::string blobName
      = blobNamePrefix + std::to_string(blockSize) + "*" + std::to_string(numBlocks);

  BlobClientOptions clientOptions;
  clientOptions.Retry.MaxRetries = 0;
  auto client = BlockBlobClient(
      std::string("https://") + accountName + ".blob.core.windows.net/" + containerName + "/"
          + blobName,
      cred,
      clientOptions);

  std::string blobContent;
  blobContent.resize(blockSize * numBlocks);
  FillBuffer(&blobContent[0], blobContent.size());

  UploadBlockBlobFromOptions options;
  options.TransferOptions.ChunkSize = blockSize;
  options.TransferOptions.Concurrency = concurrency;
  auto start = std::chrono::steady_clock::now();
  client.UploadFrom(
      reinterpret_cast<const uint8_t*>(blobContent.data()), blobContent.size(), options);
  auto end = std::chrono::steady_clock::now();
  int ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  return ms;
}
