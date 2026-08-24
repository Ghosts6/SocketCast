cmake_minimum_required(VERSION 3.16)
project(socketcast_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SOCKETCAST_BUILD_TESTS "Build unit tests" OFF)
option(SOCKETCAST_BUILD_FUZZ "Build fuzz targets (requires clang + libFuzzer)" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# --- core library ---
add_library(socketcast_core
    src/packet.cpp
    src/session.cpp
    src/transport.cpp
    src/jitter_buffer.cpp
    src/rate_controller.cpp
    src/rto.cpp
)
target_include_directories(socketcast_core PUBLIC include)

# --- engine executable ---
add_executable(socketcast_engine src/main.cpp)
target_link_libraries(socketcast_engine PRIVATE socketcast_core)

# --- tests ---
if(SOCKETCAST_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# --- fuzz targets ---
if(SOCKETCAST_BUILD_FUZZ)
  add_executable(fuzz_packet_parser fuzz/fuzz_packet_parser.cpp)
  target_include_directories(fuzz_packet_parser PRIVATE include)
  target_link_libraries(fuzz_packet_parser PRIVATE socketcast_core)
  target_compile_options(fuzz_packet_parser PRIVATE -g -O1 -fsanitize=fuzzer,address)
  target_link_options(fuzz_packet_parser PRIVATE -fsanitize=fuzzer,address)
endif()
