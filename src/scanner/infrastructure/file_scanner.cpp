#include "scanner/infrastructure/file_scanner.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "scanner/application/i_logger.h"
#include "scanner/application/thread_pool.h"
#include "scanner/domain/utilities.h"

namespace scanner {
namespace infrastructure {

namespace fs = std::filesystem;

namespace {

// === 内部扫描统计收集器 ===
// 线程安全的统计聚合
struct ScanStats {
    std::atomic<std::uint64_t> file_count{0};
    std::atomic<std::uint64_t> total_bytes{0};
    std::atomic<std::uint64_t> error_count{0};

    std::mutex extension_mutex;
    std::map<std::string, std::uint64_t> extension_counts;
    std::vector<domain::FileMetadata> files;

    std::mutex error_mutex;
    std::vector<domain::ScanError> errors;

    void record_file(const fs::path& path,
                     std::uintmax_t size,
                     std::string extension) {
        file_count.fetch_add(1, std::memory_order_relaxed);
        total_bytes.fetch_add(size, std::memory_order_relaxed);

        std::lock_guard<std::mutex> lock(extension_mutex);
        ++extension_counts[extension];
        files.push_back(
            {domain::display_path(path), size, std::move(extension)});
    }

    void record_error(const fs::path& path,
                      std::string message,
                      application::ILogger* logger) {
        error_count.fetch_add(1, std::memory_order_relaxed);

        domain::ScanError error{domain::display_path(path),
                                std::move(message)};
        {
            std::lock_guard<std::mutex> lock(error_mutex);
            errors.push_back(error);
        }
        if (logger) {
            logger->log(error.path, error.message);
        }
    }
};

}  // namespace

FileScanner::FileScanner(application::ILogger* logger) : logger_(logger) {}

domain::ScanResult FileScanner::scan(const domain::ScanOptions& options) {
    const auto throw_scan_error = [&](const std::string& message) {
        if (logger_) {
            logger_->log(domain::display_path(options.root), message);
        }
        throw std::invalid_argument(message);
    };

    if (options.worker_count == 0 || options.worker_count > 64) {
        throw_scan_error("线程数必须是 1 到 64 之间的整数");
    }

    std::error_code root_error;
    if (!fs::exists(options.root, root_error) || root_error) {
        throw_scan_error("目录不存在或无法访问: " +
                         domain::display_path(options.root));
    }
    if (!fs::is_directory(options.root, root_error) || root_error) {
        throw_scan_error("目标不是目录: " +
                         domain::display_path(options.root));
    }

    ScanStats stats;
    const std::size_t queue_capacity =
        std::max<std::size_t>(options.worker_count * 8, 64);
    application::ThreadPool thread_pool(options.worker_count, queue_capacity);
    const auto start_time = std::chrono::steady_clock::now();

    std::error_code iterator_error;
    const auto iterator_options =
        fs::directory_options::skip_permission_denied;
    bool cancelled = false;

    for (fs::recursive_directory_iterator it(
             options.root, iterator_options, iterator_error),
         end;
         it != end; it.increment(iterator_error)) {
        if (options.cancel &&
            options.cancel->load(std::memory_order_relaxed)) {
            cancelled = true;
            break;
        }

        if (iterator_error) {
            stats.record_error(
                options.root,
                "目录遍历失败: " + iterator_error.message(),
                logger_);
            iterator_error.clear();
            continue;
        }

        std::error_code entry_error;
        if (it->is_regular_file(entry_error)) {
            fs::path file;
            try {
                file = it->path();
                const std::string extension =
                    domain::extension_for_path(file);
                if (!options.filter.extensions.empty() &&
                    options.filter.extensions.find(extension) ==
                        options.filter.extensions.end()) {
                    continue;
                }
                if (options.discovered_files) {
                    options.discovered_files->fetch_add(
                        1, std::memory_order_relaxed);
                }

                const auto minimum_size = options.filter.min_size;
                const auto maximum_size = options.filter.max_size;
                thread_pool.enqueue_with_cancel(
                    options.cancel,
                    [file = std::move(file), extension, minimum_size,
                     maximum_size, &stats, cancel = options.cancel,
                     logger = logger_] {
                        if (cancel &&
                            cancel->load(std::memory_order_relaxed)) {
                            return;
                        }

                        std::error_code file_error;
                        const std::uintmax_t size =
                            fs::file_size(file, file_error);
                        if (file_error) {
                            stats.record_error(
                                file,
                                "读取文件大小失败: " + file_error.message(),
                                logger);
                            return;
                        }
                        if ((minimum_size && size < *minimum_size) ||
                            (maximum_size && size > *maximum_size)) {
                            return;
                        }
                        stats.record_file(file, size, extension);
                    });
            } catch (const std::exception& error) {
                if (options.cancel &&
                    options.cancel->load(std::memory_order_relaxed)) {
                    cancelled = true;
                    break;
                }
                stats.record_error(
                    file,
                    "提交文件任务失败: " + std::string(error.what()),
                    logger_);
            }
        } else if (entry_error) {
            stats.record_error(
                it->path(),
                "读取目录项失败: " + entry_error.message(),
                logger_);
        }
    }

    if (iterator_error) {
        stats.record_error(options.root,
                           "目录遍历失败: " + iterator_error.message(),
                           logger_);
    }

    thread_pool.shutdown();

    const auto end_time = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration<double>(end_time - start_time).count();

    domain::ScanResult result;
    result.file_count = stats.file_count.load(std::memory_order_relaxed);
    result.total_bytes = stats.total_bytes.load(std::memory_order_relaxed);
    result.error_count = stats.error_count.load(std::memory_order_relaxed);
    result.elapsed_seconds = elapsed;
    result.cancelled = cancelled;
    result.extension_counts = std::move(stats.extension_counts);
    result.files = std::move(stats.files);
    result.errors = std::move(stats.errors);
    result.log_write_failures =
        logger_ ? logger_->failure_count() : 0;
    return result;
}

}  // namespace infrastructure
}  // namespace scanner
