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
    src/scanner/application/tool_registry.cpp
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
    src/scanner/infrastructure/duplicate_finder.cpp
    src/scanner/infrastructure/database_storage.cpp
    src/scanner/infrastructure/json_exporter.cpp
    src/scanner/infrastructure/file_watcher.cpp
    src/scanner/infrastructure/file_cleaner.cpp
    src/scanner/infrastructure/tool_factories.cpp
)
target_include_directories(scanner_infrastructure PUBLIC include)
target_link_libraries(scanner_infrastructure PUBLIC scanner_application)

# 查找并链接 OpenSSL (SHA256)
find_package(OpenSSL REQUIRED)
target_link_libraries(scanner_infrastructure PRIVATE OpenSSL::Crypto)

# 查找并链接 SQLite
find_package(SQLite3 REQUIRED)
target_link_libraries(scanner_infrastructure PRIVATE SQLite3::SQLite3)

# ============================================================
# 可执行文件
# ============================================================

# CLI 版本
add_executable(file_scanner src/main.cpp)
target_link_libraries(file_scanner PRIVATE scanner_infrastructure)

# GUI 版本 (Win32)
add_executable(file_scanner_gui WIN32 src/gui_main_win32.cpp)
target_link_libraries(file_scanner_gui PRIVATE scanner_infrastructure comctl32 shell32 ole32 dwmapi)

# Web GUI 版本 (现代化浏览器界面 — 玻璃拟态设计)
add_executable(file_scanner_web WIN32 src/web_gui_main.cpp)
target_link_libraries(file_scanner_web PRIVATE scanner_infrastructure ws2_32)

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
set_scanner_warnings(file_scanner_web)

if (MINGW)
    # 宽字符入口（Windows Unicode 支持）
    target_link_options(file_scanner PRIVATE -municode)
    target_link_options(file_scanner_gui PRIVATE -municode)
    # 静态链接 C++ 标准库和 GCC 运行时
    target_link_options(file_scanner PRIVATE -static-libstdc++ -static-libgcc)
    target_link_options(file_scanner_gui PRIVATE -static-libstdc++ -static-libgcc)
    target_link_options(file_scanner_web PRIVATE -static-libstdc++ -static-libgcc -municode)

    # 复制 libwinpthread-1.dll 到构建目录（clock_gettime64 需要）
    # GCC 链接器无法静态链接 winpthread，需要运行时 DLL
    add_custom_command(TARGET file_scanner POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "F:/msys64/ucrt64/bin/libwinpthread-1.dll"
            $<TARGET_FILE_DIR:file_scanner>
        COMMENT "Copying libwinpthread-1.dll"
    )
    add_custom_command(TARGET file_scanner_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "F:/msys64/ucrt64/bin/libwinpthread-1.dll"
            $<TARGET_FILE_DIR:file_scanner_gui>
        COMMENT "Copying libwinpthread-1.dll"
    )
    add_custom_command(TARGET file_scanner_web POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "F:/msys64/ucrt64/bin/libwinpthread-1.dll"
            $<TARGET_FILE_DIR:file_scanner_web>
        COMMENT "Copying libwinpthread-1.dll"
    )
    add_custom_command(TARGET file_scanner_web POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/gui"
            "$<TARGET_FILE_DIR:file_scanner_web>/gui"
        COMMENT "Copying gui/ directory"
    )
endif()

# ============================================================
# 测试（自包含测试框架，无需外部依赖）
# ============================================================
enable_testing()

function(add_scanner_test test_name test_source)
    add_executable(${test_name} ${test_source} tests/minitest.cpp)
    target_link_libraries(${test_name} PRIVATE scanner_infrastructure)
    target_include_directories(${test_name} PRIVATE include tests)
    if (MINGW)
        target_link_options(${test_name} PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    set_scanner_warnings(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_scanner_test(test_domain_utilities tests/test_domain_utilities.cpp)
add_scanner_test(test_thread_pool tests/test_thread_pool.cpp)
add_scanner_test(test_csv_exporter tests/test_csv_exporter.cpp)
add_scanner_test(test_file_scanner tests/test_file_scanner.cpp)
add_scanner_test(test_duplicate_finder tests/test_duplicate_finder.cpp)
add_scanner_test(test_file_cleaner tests/test_file_cleaner.cpp)
add_scanner_test(test_tool_registry tests/test_tool_registry.cpp)
