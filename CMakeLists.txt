cmake_minimum_required(VERSION 3.6)

project(azure-sdk-for-cpp-perf)
set(CMAKE_CXX_STANDARD 14)

add_subdirectory(azure-sdk-for-cpp)
add_subdirectory(azure-storage-cpplite)

set(PERF_TEST_SOURCE
    src/main.cc
    src/credential.h
    src/track1_test.h
    src/track1_test.cc
    src/track2_test.h
    src/track2_test.cc
)

add_executable(perftest ${PERF_TEST_SOURCE})

if(WIN32)
  target_compile_definitions(perftest PRIVATE NOMINMAX)
endif()

target_link_libraries(perftest azure-storage-lite azure::storage::blobs)
