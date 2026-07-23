cmake_minimum_required(VERSION 3.20)

project(cpp_file_scanner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(file_scanner
    src/main.cpp
    src/thread_pool.cpp
)

target_include_directories(file_scanner PRIVATE include)

if (MSVC)
    target_compile_options(file_scanner PRIVATE /W4 /permissive-)
else()
    target_compile_options(file_scanner PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (MINGW)
    target_link_options(file_scanner PRIVATE -municode)
endif()
