cmake_minimum_required(VERSION 3.20)
project(Project-SOS-Backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(src/common)
add_subdirectory(src/room)
add_subdirectory(src/chat)

enable_testing()
add_subdirectory(tests)
