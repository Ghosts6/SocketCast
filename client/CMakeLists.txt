cmake_minimum_required(VERSION 3.16)
project(socketcast_client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add engine as dependency (for packet, session, transport)
add_subdirectory(../engine engine)

# Optional SDL2 support (Phase 4+)
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    message(STATUS "SDL2 found — building with video rendering support")
    set(SOCKETCAST_SDL2 ON)
else()
    message(STATUS "SDL2 not found — building in headless mode (stats only)")
    set(SOCKETCAST_SDL2 OFF)
endif()

add_library(socketcast_client_lib src/client.cpp)
target_include_directories(socketcast_client_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(socketcast_client_lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../engine/include)
target_link_libraries(socketcast_client_lib PRIVATE socketcast_core)

if(SOCKETCAST_SDL2)
    target_compile_definitions(socketcast_client_lib PRIVATE SOCKETCAST_SDL2)
    target_link_libraries(socketcast_client_lib PRIVATE SDL2::SDL2)
endif()

add_executable(socketcast_client src/main.cpp)
target_include_directories(socketcast_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../engine/include)
target_link_libraries(socketcast_client PRIVATE socketcast_client_lib)
