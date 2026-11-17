cmake_minimum_required(VERSION 4.3)
project(Instantee VERSION 0.0.0 LANGUAGES C CXX OBJCXX)

foreach(language IN ITEMS CXX OBJCXX)
  set(CMAKE_${language}_STANDARD 23)
  set(CMAKE_${language}_STANDARD_REQUIRED ON)
  set(CMAKE_${language}_EXTENSIONS OFF)
endforeach()
include(cmake/VerifyToolchain.cmake)
include(cmake/ProjectOptions.cmake)
include(cmake/ModuleBoundaries.cmake)
enable_testing()
include(cmake/ToolchainProbes.cmake)

add_subdirectory(third_party/yyjson)

# Pinned Google Benchmark for CPU micrometrics. Never enable its test suite or
# install rules inside the product tree.
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(BENCHMARK_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/google-benchmark EXCLUDE_FROM_ALL)

add_subdirectory(engine)
add_subdirectory(benchmarks)
