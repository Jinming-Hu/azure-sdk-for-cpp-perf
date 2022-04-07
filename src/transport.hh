#pragma once

#include <memory>
#include <string>
#include <vector>

class transport {
public:
  const std::string name;

  virtual void reset(int /* concurrency */) {}
  virtual void download_blob(const std::string& blob_name, uint8_t* buffer, size_t blob_size) = 0;
  virtual void upload_blob(const std::string& blob_name, const uint8_t* buffer, size_t blob_size)
      = 0;
  virtual ~transport() {}

protected:
  transport(std::string name) : name(std::move(name)) {}
};

class cpplite_transport : public transport {
public:
  cpplite_transport() : transport("cpplite") {}

private:
  void reset(int concurrency) override;
  void download_blob(const std::string& blob_name, uint8_t* buffer, size_t blob_size) override;
  void upload_blob(const std::string& blob_name, const uint8_t* buffer, size_t blob_size) override;
  std::shared_ptr<void> m_blob_service_client;
};

class track2_transport : public transport {
public:
  void download_blob(const std::string& blob_name, uint8_t* buffer, size_t blob_size) override;
  void upload_blob(const std::string& blob_name, const uint8_t* buffer, size_t blob_size) override;

protected:
  track2_transport(std::string name) : transport(std::move(name)) {}
  std::shared_ptr<void> m_container_client;
};

class track2_curl_transport : public track2_transport {
public:
  track2_curl_transport();
};

#if defined(_WIN32)

class track2_winhttp_transport : public track2_transport {
public:
  track2_winhttp_transport();
};

#endif
