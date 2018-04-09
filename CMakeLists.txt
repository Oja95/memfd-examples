cmake_minimum_required(VERSION 3.8)
project(memfd_example_cmake)

set(CMAKE_CXX_STANDARD 17)

set(SOURCE_FILES server.cpp memfd.hpp)
add_executable(server ${SOURCE_FILES})
set(SOURCE_FILES client.cpp memfd.hpp)
add_executable(client ${SOURCE_FILES})