cmake_minimum_required(VERSION 3.20)
project(order_matching_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(LOB_ENABLE_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(LOB_BUILD_TESTS "Build the test suite" ON)
option(LOB_BUILD_BENCHMARKS "Build the Google Benchmark suite" ON)

# ThreadSanitizer can't coexist with ASan/UBSan in the same binary, so the
# SPSC ring buffer's concurrency test gets its own separate executable,
# built only when this is explicitly requested -- configure with
# -DLOB_ENABLE_SANITIZERS=OFF (the default) alongside this, not both ON.
option(LOB_BUILD_TSAN_TESTS "Build a ThreadSanitizer-only binary for the SPSC ring buffer test" OFF)

find_package(Threads REQUIRED)

# libFuzzer (-fsanitize=fuzzer) requires real LLVM clang's compiler-rt
# runtime; AppleClang (macOS system clang) doesn't ship it. Default ON only
# when the compiler actually reports as "Clang" (true for apt's clang on
# Ubuntu CI, and for a brew-installed llvm pointed at explicitly via
# -DCMAKE_CXX_COMPILER) -- OFF by default for AppleClang/GCC.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  option(LOB_BUILD_FUZZERS "Build the libFuzzer harnesses" ON)
else()
  option(LOB_BUILD_FUZZERS "Build the libFuzzer harnesses" OFF)
endif()

add_library(lob_warnings INTERFACE)
target_compile_options(lob_warnings INTERFACE -Wall -Wextra -Werror)

add_library(lob_sanitizers INTERFACE)
if(LOB_ENABLE_SANITIZERS)
  target_compile_options(lob_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(lob_sanitizers INTERFACE -fsanitize=address,undefined)
endif()

add_subdirectory(src)
add_subdirectory(sim)
add_subdirectory(mm)

if(LOB_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  # Windows-only setting; harmless elsewhere.
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_subdirectory(tests)
endif()

if(LOB_BUILD_BENCHMARKS)
  include(FetchContent)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(googlebenchmark)

  add_subdirectory(benchmarks)
endif()
