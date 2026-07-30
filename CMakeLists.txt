cmake_minimum_required(VERSION 3.20)

project(cpp_file_scanner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Layer 1: Domain — 核心数据类型 + 纯函数工具（零依赖）
# ============================================================
add_library(scanner_domain STATIC
    src/scanner/domain/utilities.cpp
)
target_include_directories(scanner_domain PUBLIC include)

# ============================================================
# Layer 2: Application — 接口定义 + 线程池（仅依赖 Domain）
# ============================================================
add_library(scanner_application STATIC
    src/scanner/application/thread_pool.cpp
)
target_include_directories(scanner_application PUBLIC include)
target_link_libraries(scanner_application PUBLIC scanner_domain)

# ============================================================
# Layer 3: Infrastructure — 具体实现（依赖 Application + Domain）
# ============================================================
add_library(scanner_infrastructure STATIC
    src/scanner/infrastructure/file_scanner.cpp
    src/scanner/infrastructure/csv_exporter.cpp
    src/scanner/infrastructure/error_logger.cpp
)
target_include_directories(scanner_infrastructure PUBLIC include)
target_link_libraries(scanner_infrastructure PUBLIC scanner_application)

# ============================================================
# 可执行文件
# ============================================================

# CLI 版本
add_executable(file_scanner src/main.cpp)
target_link_libraries(file_scanner PRIVATE scanner_infrastructure)

# GUI 版本 (Win32)
add_executable(file_scanner_gui WIN32 src/gui_main_win32.cpp)
target_link_libraries(file_scanner_gui PRIVATE scanner_infrastructure comctl32 shell32 ole32)

# ============================================================
# 编译选项
# ============================================================
function(set_scanner_warnings target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

set_scanner_warnings(scanner_domain)
set_scanner_warnings(scanner_application)
set_scanner_warnings(scanner_infrastructure)
set_scanner_warnings(file_scanner)
set_scanner_warnings(file_scanner_gui)

if (MINGW)
    target_link_options(file_scanner PRIVATE -municode)
    target_link_options(file_scanner_gui PRIVATE -municode)
endif()
