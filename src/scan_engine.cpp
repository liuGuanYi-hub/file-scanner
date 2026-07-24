#include "scan_engine.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "thread_pool.h"

namespace scanner {

namespace {

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

template <typename String>
std::size_t parse_worker_count_impl(const String& value) {
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

}  // namespace

std::string display_path(const fs::path& path) {
    try {
        return path.u8string();
    } catch (const std::exception&) {
        return "<无法显示的路径>";
    }
}

std::size_t default_worker_count() {
    const unsigned int cpu_count = std::thread::hardware_concurrency();
    if (cpu_count == 0) {
        return 2;
    }
    return std::min<std::size_t>(cpu_count, 8);
}

std::size_t parse_worker_count(const std::string& value) {
    return parse_worker_count_impl(value);
}

#ifdef _WIN32
std::size_t parse_worker_count(const std::wstring& value) {
    return parse_worker_count_impl(value);
}
#endif

ScanResult scan_directory(const ScanOptions& options) {
    if (options.worker_count == 0 || options.worker_count > 64) {
        throw std::invalid_argument("线程数必须是 1 到 64 之间的整数");
    }

    std::error_code root_error;
    if (!fs::exists(options.root, root_error) || root_error) {
        throw std::invalid_argument("目录不存在或无法访问: " + display_path(options.root));
    }
    if (!fs::is_directory(options.root, root_error) || root_error) {
        throw std::invalid_argument("目标不是目录: " + display_path(options.root));
    }

    ScanStats stats;
    ThreadPool thread_pool(options.worker_count);
    const auto start_time = std::chrono::steady_clock::now();

    std::error_code iterator_error;
    const auto iterator_options = fs::directory_options::skip_permission_denied;
    bool cancelled = false;

    for (fs::recursive_directory_iterator it(options.root, iterator_options, iterator_error), end;
         it != end;
         it.increment(iterator_error)) {
        if (options.cancel && options.cancel->load(std::memory_order_relaxed)) {
            cancelled = true;
            break;
        }

        if (iterator_error) {
            stats.error_count.fetch_add(1, std::memory_order_relaxed);
            iterator_error.clear();
            continue;
        }

        std::error_code entry_error;
        if (it->is_regular_file(entry_error)) {
            try {
                fs::path file = it->path();
                if (options.discovered_files) {
                    options.discovered_files->fetch_add(1, std::memory_order_relaxed);
                }

                thread_pool.enqueue([file = std::move(file), &stats, cancel = options.cancel] {
                    if (cancel && cancel->load(std::memory_order_relaxed)) {
                        return;
                    }

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

    ScanResult result;
    result.file_count = stats.file_count.load(std::memory_order_relaxed);
    result.total_bytes = stats.total_bytes.load(std::memory_order_relaxed);
    result.error_count = stats.error_count.load(std::memory_order_relaxed);
    result.elapsed_seconds = elapsed;
    result.cancelled = cancelled;
    result.extension_counts = std::move(stats.extension_counts);
    return result;
}

}  // namespace scanner
