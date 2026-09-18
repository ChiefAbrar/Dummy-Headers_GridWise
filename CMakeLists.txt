cmake_minimum_required(VERSION 3.20)
project(gridwise_cpp VERSION 1.0.0 LANGUAGES CXX)
cmake_policy(SET CMP0167 NEW)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GRIDWISE_NATIVE "Enable -march=native for local-only builds" OFF)
option(GRIDWISE_LTO "Enable link-time optimization" ON)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(Boost 1.83 REQUIRED)
find_library(GRIDWISE_CURL_SHARED NAMES curl PATHS /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu NO_DEFAULT_PATH)
if(NOT GRIDWISE_CURL_SHARED)
  set(GRIDWISE_CURL_SHARED CURL::libcurl)
endif()

add_library(gridwise_core
    src/config.cpp
    src/llm_client.cpp
    src/optimizer.cpp
    src/replay.cpp
    src/validation.cpp
    src/engine.cpp
    src/boost_json.cpp
)

target_include_directories(gridwise_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(gridwise_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
target_link_libraries(gridwise_core PUBLIC ${GRIDWISE_CURL_SHARED} Threads::Threads)
target_compile_definitions(gridwise_core PUBLIC BOOST_ERROR_CODE_HEADER_ONLY=1 BOOST_SYSTEM_NO_DEPRECATED=1)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gridwise_core PRIVATE -O3 -ffunction-sections -fdata-sections)
    if(GRIDWISE_LTO)
        target_compile_options(gridwise_core PRIVATE -flto)
        target_link_options(gridwise_core PRIVATE -flto)
    endif()
endif()

if(GRIDWISE_NATIVE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gridwise_core PRIVATE -march=native -mtune=native)
endif()

add_executable(gridwise_server src/main.cpp)
target_link_libraries(gridwise_server PRIVATE gridwise_core)

add_executable(gridwise_tests EXCLUDE_FROM_ALL tests/core_tests.cpp)
target_link_libraries(gridwise_tests PRIVATE gridwise_core)

install(TARGETS gridwise_server RUNTIME DESTINATION bin)