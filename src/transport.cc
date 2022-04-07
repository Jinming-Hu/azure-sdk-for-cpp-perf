#include "transport.hh"

#if defined(_WIN32)
#include <azure/core/http/win_http_transport.hpp>
#endif
#include <azure/core/http/curl_transport.hpp>
#include <azure/storage/blobs.hpp>
#include <blob/blob_client.h>
#include <mstream.h>

#include "constants.hh"
#include "utilities.hh"

void cpplite_transport::reset(int concurrency)
{
  using namespace azure::storage_lite;
  auto cred = std::make_shared<shared_key_credential>(
      get_account_name_from_connection_string(), get_access_key_from_connection_string());
  auto account = std::make_shared<storage_account>(
      get_account_name_from_connection_string(), cred, /* use_https */ true);
  auto blob_service_client = std::make_shared<blob_client>(account, concurrency);
  blob_service_client->context()->set_retry_policy(std::make_shared<no_retry_policy>());
  m_blob_service_client = blob_service_client;
}

void cpplite_transport::download_blob(
    const std::string& blob_name,
    uint8_t* buffer,
    size_t blob_size)
{
  using namespace azure::storage_lite;

  auto blob_service_client = std::static_pointer_cast<blob_client>(m_blob_service_client);
  omstream os(reinterpret_cast<char*>(buffer), blob_size);
  auto ret
      = blob_service_client->download_blob_to_stream(container_name, blob_name, 0, blob_size, os)
            .get();
  if (!ret.success())
  {
    throw storage_exception(
        std::stoi(ret.error().code), ret.error().code_name, ret.error().message);
  }
}

void cpplite_transport::upload_blob(
    const std::string& blob_name,
    const uint8_t* buffer,
    size_t blob_size)
{
  using namespace azure::storage_lite;

  auto blob_service_client = std::static_pointer_cast<blob_client>(m_blob_service_client);
  imstream is(reinterpret_cast<const char*>(buffer), blob_size);
  auto ret
      = blob_service_client->upload_block_blob_from_stream(container_name, blob_name, is, {}).get();
  if (!ret.success())
  {
    throw storage_exception(
        std::stoi(ret.error().code), ret.error().code_name, ret.error().message);
  }
}

void track2_transport::download_blob(
    const std::string& blob_name,
    uint8_t* buffer,
    size_t blob_size)
{
  using namespace Azure::Storage::Blobs;

  auto container_client = std::static_pointer_cast<BlobContainerClient>(m_container_client);
  auto blob_client = container_client->GetBlobClient(blob_name);
  DownloadBlobToOptions options;
  options.TransferOptions.InitialChunkSize = blob_size;
  options.TransferOptions.ChunkSize = blob_size;
  options.TransferOptions.Concurrency = 1;
  blob_client.DownloadTo(buffer, blob_size, options);
}

void track2_transport::upload_blob(
    const std::string& blob_name,
    const uint8_t* buffer,
    size_t blob_size)
{
  using namespace Azure::Storage::Blobs;

  auto container_client = std::static_pointer_cast<BlobContainerClient>(m_container_client);
  auto blob_client = container_client->GetBlockBlobClient(blob_name);
  UploadBlockBlobFromOptions options;
  options.TransferOptions.ChunkSize = blob_size;
  options.TransferOptions.Concurrency = 1;
  blob_client.UploadFrom(buffer, blob_size, options);
}

track2_curl_transport::track2_curl_transport() : track2_transport("Track2(curl)")
{
  using namespace Azure::Storage::Blobs;

  BlobClientOptions clientOptions;
  clientOptions.Retry.MaxRetries = 0;
  clientOptions.Transport.Transport = std::make_shared<Azure::Core::Http::CurlTransport>();
  auto container_client = BlobContainerClient::CreateFromConnectionString(
      connection_string, container_name, clientOptions);
  m_container_client = std::make_shared<BlobContainerClient>(std::move(container_client));
}

#if defined(_WIN32)

track2_winhttp_transport::track2_winhttp_transport() : track2_transport("Track2(WinHTTP)")
{
  using namespace Azure::Storage::Blobs;

  BlobClientOptions clientOptions;
  clientOptions.Retry.MaxRetries = 0;
  clientOptions.Transport.Transport = std::make_shared<Azure::Core::Http::WinHttpTransport>();
  auto container_client = BlobContainerClient::CreateFromConnectionString(
      connection_string, container_name, clientOptions);
  m_container_client = std::make_shared<BlobContainerClient>(std::move(container_client));
}

#endif