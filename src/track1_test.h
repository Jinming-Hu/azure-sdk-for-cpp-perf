#pragma once

#include <cstdint>

int track1_test_download(int64_t blobSize, int numBlobs, int concurrency);
int track1_test_upload(int64_t blobSize, int numBlobs, int concurrency);