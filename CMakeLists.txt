cmake_minimum_required(VERSION 3.11)
set(CMAKE_CXX_STANDARD 14)

project(azure-sdk-for-cpp-perf)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake-modules")

find_package(Git REQUIRED)
include(FetchContent)
include(automate-vcpkg)

vcpkg_bootstrap()
vcpkg_install_packages(curl spdlog nlohmann-json cryptopp)
if(WIN32)
  vcpkg_install_packages(wil)
else()
  vcpkg_install_packages(libxml2)
endif()

FetchContent_Declare(
  azure-core
  GIT_REPOSITORY https://github.com/Azure/azure-sdk-for-cpp.git
  GIT_TAG azure-core_1.8.0-beta.2)
FetchContent_Declare(
  azure-storage-common
  GIT_REPOSITORY https://github.com/Azure/azure-sdk-for-cpp.git
  GIT_TAG azure-storage-common_12.3.0)
FetchContent_Declare(
  azure-storage-blobs
  GIT_REPOSITORY https://github.com/Azure/azure-sdk-for-cpp.git
  GIT_TAG azure-storage-blobs_12.6.2)

function(get_dependency_git_version source_directory tag_pattern
         dependency_git_version)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match --match
            "${tag_pattern}" --dirty
    WORKING_DIRECTORY ${source_directory}
    OUTPUT_VARIABLE gitversion
    RESULT_VARIABLE ret
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(ret EQUAL "0")
    set(${dependency_git_version}
        ${gitversion}
        PARENT_SCOPE)
    return()
  endif()
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --match="" --always --dirty
    WORKING_DIRECTORY ${source_directory}
    OUTPUT_VARIABLE gitversion
    RESULT_VARIABLE ret
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(ret EQUAL "0")
    set(${dependency_git_version}
        ${gitversion}
        PARENT_SCOPE)
  else()
    set(${dependency_git_version}
        "unknown"
        PARENT_SCOPE)
  endif()
endfunction()

set(AZ_ALL_LIBRARIES ON)
set(BUILD_TRANSPORT_CURL ON)
if(WIN32)
  set(BUILD_TRANSPORT_WINHTTP ON)
endif()

FetchContent_GetProperties(azure-core)
if(NOT azure-core_POPULATED)
  FetchContent_Populate(azure-core)
  add_subdirectory(${azure-core_SOURCE_DIR}/sdk/core/azure-core
                   ${azure-core_BINARY_DIR})
endif()

FetchContent_GetProperties(azure-storage-common)
if(NOT azure-storage-common_POPULATED)
  FetchContent_Populate(azure-storage-common)
  add_subdirectory(
    ${azure-storage-common_SOURCE_DIR}/sdk/storage/azure-storage-common
    ${azure-storage-common_BINARY_DIR})
endif()

FetchContent_GetProperties(azure-storage-blobs)
if(NOT azure-storage-blobs_POPULATED)
  FetchContent_Populate(azure-storage-blobs)
  add_subdirectory(
    ${azure-storage-blobs_SOURCE_DIR}/sdk/storage/azure-storage-blobs
    ${azure-storage-blobs_BINARY_DIR})
endif()

FetchContent_Declare(
  azure-storage-cpplite
  URL https://codeload.github.com/Azure/azure-storage-cpplite/zip/refs/heads/master
)
FetchContent_MakeAvailable(azure-storage-cpplite)
file(READ ${azure-storage-cpplite_SOURCE_DIR}/include/constants.dat filedata)
string(
  REGEX
  REPLACE "DAT\\(header_value_storage_blob_version, \"[0-9-]+\"\\)"
          "DAT\(header_value_storage_blob_version, \"2019-12-12\"\)" filedata
          "${filedata}")
file(WRITE ${azure-storage-cpplite_SOURCE_DIR}/include/constants.dat
     "${filedata}")

set(PERF_TEST_SOURCE
    src/main.cc
    src/cases.hh
    src/cases.cc
    src/utilities.hh
    src/utilities.cc
    src/constants.hh
    src/transport.hh
    src/transport.cc)

add_executable(perftest ${PERF_TEST_SOURCE})

if(WIN32)
  target_compile_definitions(perftest PRIVATE NOMINMAX)
endif()

if(MSVC)
  target_compile_options(perftest PUBLIC /W4 /WX /MP)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL
                                                "GNU")
  target_compile_options(azure-storage-lite PRIVATE -Wno-error)
  target_compile_options(perftest PUBLIC -Wall -Wextra -Werror -pedantic)
  target_compile_options(perftest PRIVATE -O3)
endif()

get_dependency_git_version(${azure-core_SOURCE_DIR} "azure-core_*"
                           AZURE_CORE_GIT_VERSION)
get_dependency_git_version(
  ${azure-storage-common_SOURCE_DIR} "azure-storage-common_*"
  AZURE_STORAGE_COMMON_GIT_VERSION)
get_dependency_git_version(
  ${azure-storage-blobs_SOURCE_DIR} "azure-storage-blobs_*"
  AZURE_STORAGE_BLOBS_GIT_VERSION)

target_compile_definitions(
  perftest
  PRIVATE
    BUILD_OS_VERSION="${CMAKE_SYSTEM} ${CMAKE_SYSTEM_PROCESSOR}"
    BUILD_COMPILER_VERSION="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    AZURE_CORE_GIT_VERSION="${AZURE_CORE_GIT_VERSION}"
    AZURE_STORAGE_COMMON_GIT_VERSION="${AZURE_STORAGE_COMMON_GIT_VERSION}"
    AZURE_STORAGE_BLOBS_GIT_VERSION="${AZURE_STORAGE_BLOBS_GIT_VERSION}")

find_package(CURL CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(cryptopp CONFIG REQUIRED)

target_link_libraries(
  perftest
  CURL::libcurl
  spdlog::spdlog
  nlohmann_json::nlohmann_json
  cryptopp-static
  azure-storage-lite
  Azure::azure-storage-blobs)
