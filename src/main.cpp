#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

#include "scan_engine.h"

namespace fs = std::filesystem;

void print_usage() {
    std::cout << "用法: file_scanner.exe [扫描目录] [线程数]\n"
              << "示例: file_scanner.exe . 4\n";
}

template <typename String>
int run_scanner(int argc, String* argv[]) {
    if (argc > 3) {
        print_usage();
        return 1;
    }

    fs::path root = fs::current_path();
    if (argc >= 2) {
        root = fs::path(argv[1]);
    }

    std::size_t worker_count = scanner::default_worker_count();
    if (argc == 3) {
        try {
#ifdef _WIN32
            worker_count = scanner::parse_worker_count(std::wstring(argv[2]));
#else
            worker_count = scanner::parse_worker_count(argv[2]);
#endif
        } catch (const std::exception& error) {
            std::cerr << "参数错误: " << error.what() << "\n";
            return 1;
        }
    }

    try {
        const scanner::ScanResult result = scanner::scan_directory({root, worker_count});

        std::cout << "\n扫描完成\n"
                  << "目录: " << scanner::display_path(root) << "\n"
                  << "线程数: " << worker_count << "\n"
                  << "文件数: " << result.file_count << "\n"
                  << "总大小: " << std::fixed << std::setprecision(2)
                  << static_cast<double>(result.total_bytes) / (1024.0 * 1024.0)
                  << " MiB\n"
                  << "错误数: " << result.error_count << "\n"
                  << "耗时: " << result.elapsed_seconds << " 秒\n\n"
                  << "扩展名统计:\n";

        for (const auto& [extension, count] : result.extension_counts) {
            std::cout << "  " << std::setw(16) << std::left << extension
                      << count << " 个\n";
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }

    return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    return run_scanner(argc, argv);
}
#else
int main(int argc, char* argv[]) {
    return run_scanner(argc, argv);
}
#endif
