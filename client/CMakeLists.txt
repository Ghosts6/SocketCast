cmake_minimum_required(VERSION 3.16)
project(socketcast_client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# TODO(Phase 4): find_package(SDL2 REQUIRED) — or OpenCV, pick one.
# find_package(SDL2 REQUIRED)

add_executable(socketcast_client src/main.cpp)

# TODO: link against ../engine's socketcast_core for shared packet/transport
# code once that's split into a reusable library target, and link SDL2.
# target_link_libraries(socketcast_client PRIVATE socketcast_core SDL2::SDL2)
