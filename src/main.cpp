#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "thread_pool.h"

namespace fs = std::filesystem;

std::string display_path(const fs::path& path) {
    try {
        return path.u8string();
    } catch (const std::exception&) {
        return "<无法显示的路径>";
    }
}

struct ScanStats {
    std::atomic<std::uint64_t> file_count{0};
    std::atomic<std::uint64_t> total_bytes{0};
    std::atomic<std::uint64_t> error_count{0};

    std::mutex extension_mutex;
    std::map<std::string, std::uint64_t> extension_counts;

    void record_file(const fs::path& path, std::uintmax_t size) {
        file_count.fetch_add(1, std::memory_order_relaxed);
        total_bytes.fetch_add(size, std::memory_order_relaxed);

        std::string extension = path.extension().u8string();
        if (extension.empty()) {
            extension = "[无扩展名]";
        }

        std::lock_guard<std::mutex> lock(extension_mutex);
        ++extension_counts[extension];
    }
};

std::size_t default_worker_count() {
    const unsigned int cpu_count = std::thread::hardware_concurrency();
    if (cpu_count == 0) {
        return 2;
    }
    return std::min<std::size_t>(cpu_count, 8);
}

template <typename String>
std::size_t parse_worker_count(const String& value) {
    try {
        const unsigned long parsed = std::stoul(value);
        if (parsed == 0 || parsed > 64) {
            throw std::out_of_range("worker count must be between 1 and 64");
        }
        return static_cast<std::size_t>(parsed);
    } catch (const std::exception&) {
        throw std::invalid_argument("线程数必须是 1 到 64 之间的整数");
    }
}

void print_usage() {
    std::cout << "用法: file_scanner.exe [扫描目录] [线程数]\n"
              << "示例: file_scanner.exe . 4\n";
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
    if (argc > 3) {
        print_usage();
        return 1;
    }

    fs::path root = fs::current_path();
    if (argc >= 2) {
        root = fs::path(argv[1]);
    }

    std::size_t worker_count = default_worker_count();
    if (argc == 3) {
        try {
#ifdef _WIN32
            worker_count = parse_worker_count(std::wstring(argv[2]));
#else
            worker_count = parse_worker_count(std::string(argv[2]));
#endif
        } catch (const std::exception& error) {
            std::cerr << "参数错误: " << error.what() << "\n";
            return 1;
        }
    }

    std::error_code root_error;
    if (!fs::exists(root, root_error) || root_error) {
        std::cerr << "目录不存在或无法访问: " << display_path(root) << "\n";
        return 1;
    }
    if (!fs::is_directory(root, root_error) || root_error) {
        std::cerr << "目标不是目录: " << display_path(root) << "\n";
        return 1;
    }

    ThreadPool thread_pool(worker_count);
    ScanStats stats;

    const auto start_time = std::chrono::steady_clock::now();

    std::error_code iterator_error;
    const auto iterator_options = fs::directory_options::skip_permission_denied;
    for (fs::recursive_directory_iterator it(root, iterator_options, iterator_error), end;
         it != end;
         it.increment(iterator_error)) {
        if (iterator_error) {
            stats.error_count.fetch_add(1, std::memory_order_relaxed);
            iterator_error.clear();
            continue;
        }

        std::error_code entry_error;
        if (it->is_regular_file(entry_error)) {
            try {
                fs::path file = it->path();
                thread_pool.enqueue([file = std::move(file), &stats] {
                    std::error_code file_error;
                    const std::uintmax_t size = fs::file_size(file, file_error);
                    if (file_error) {
                        stats.error_count.fetch_add(1, std::memory_order_relaxed);
                        return;
                    }
                    stats.record_file(file, size);
                });
            } catch (const std::exception&) {
                stats.error_count.fetch_add(1, std::memory_order_relaxed);
            }
        } else if (entry_error) {
            stats.error_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (iterator_error) {
        stats.error_count.fetch_add(1, std::memory_order_relaxed);
    }

    thread_pool.shutdown();

    const auto end_time = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(end_time - start_time).count();

    std::cout << "\n扫描完成\n"
              << "目录: " << display_path(root) << "\n"
              << "线程数: " << worker_count << "\n"
              << "文件数: " << stats.file_count.load() << "\n"
              << "总大小: " << std::fixed << std::setprecision(2)
              << static_cast<double>(stats.total_bytes.load()) / (1024.0 * 1024.0)
              << " MiB\n"
              << "错误数: " << stats.error_count.load() << "\n"
              << "耗时: " << elapsed << " 秒\n\n"
              << "扩展名统计:\n";

    for (const auto& [extension, count] : stats.extension_counts) {
        std::cout << "  " << std::setw(16) << std::left << extension
                  << count << " 个\n";
    }

    return 0;
}
