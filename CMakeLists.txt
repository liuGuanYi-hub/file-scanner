cmake_minimum_required(VERSION 3.20)

project(cpp_file_scanner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(scan_core STATIC
    src/error_logger.cpp
    src/scan_engine.cpp
    src/thread_pool.cpp
)

target_include_directories(scan_core PUBLIC include)

add_executable(file_scanner src/main.cpp)
target_link_libraries(file_scanner PRIVATE scan_core)

add_executable(file_scanner_gui WIN32 src/gui_main_win32.cpp)
target_link_libraries(file_scanner_gui PRIVATE scan_core comctl32 shell32 ole32)

if (MSVC)
    target_compile_options(scan_core PRIVATE /W4 /permissive-)
    target_compile_options(file_scanner PRIVATE /W4 /permissive-)
    target_compile_options(file_scanner_gui PRIVATE /W4 /permissive-)
else()
    target_compile_options(scan_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(file_scanner PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(file_scanner_gui PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (MINGW)
    target_link_options(file_scanner PRIVATE -municode)
    target_link_options(file_scanner_gui PRIVATE -municode)
endif()
