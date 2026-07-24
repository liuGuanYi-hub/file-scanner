#include "scan_engine.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#include "error_logger.h"
#include "thread_pool.h"

namespace scanner {

namespace {

struct ScanStats {
    std::atomic<std::uint64_t> file_count{0};
    std::atomic<std::uint64_t> total_bytes{0};
    std::atomic<std::uint64_t> error_count{0};

    std::mutex extension_mutex;
    std::map<std::string, std::uint64_t> extension_counts;
    std::vector<FileMetadata> files;

    std::mutex error_mutex;
    std::vector<ScanError> errors;

    void record_file(const fs::path& path,
                     std::uintmax_t size,
                     std::string extension) {
        file_count.fetch_add(1, std::memory_order_relaxed);
        total_bytes.fetch_add(size, std::memory_order_relaxed);

        std::lock_guard<std::mutex> lock(extension_mutex);
        ++extension_counts[extension];
        files.push_back({display_path(path), size, std::move(extension)});
    }

    void record_error(const fs::path& path,
                      std::string message,
                      ErrorLogger* error_logger) {
        error_count.fetch_add(1, std::memory_order_relaxed);

        ScanError error{display_path(path), std::move(message)};
        {
            std::lock_guard<std::mutex> lock(error_mutex);
            errors.push_back(error);
        }
        if (error_logger) {
            error_logger->log(error.path, error.message);
        }
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

std::string trim_ascii(std::string value) {
    const auto is_space = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char character) {
                    return !is_space(static_cast<unsigned char>(character));
                }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char character) {
                    return !is_space(static_cast<unsigned char>(character));
                }).base(),
                value.end());
    return value;
}

std::string lowercase_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string normalize_extension(std::string extension) {
    extension = lowercase_ascii(trim_ascii(std::move(extension)));
    if (extension.empty() || extension == "*") {
        return {};
    }
    if (extension == "none" || extension == "[无扩展名]") {
        return "[无扩展名]";
    }
    if (extension.front() != '.') {
        extension.insert(extension.begin(), '.');
    }
    return extension;
}

std::string extension_for_path(const fs::path& path) {
    std::string extension = normalize_extension(path.extension().u8string());
    return extension.empty() ? "[无扩展名]" : extension;
}

template <typename String>
std::optional<std::uintmax_t> parse_size_bytes_impl(const String& value) {
    if (value.empty()) {
        throw std::invalid_argument("文件大小不能是空值");
    }

    std::size_t consumed = 0;
    try {
        const unsigned long long parsed = std::stoull(value, &consumed);
        if (consumed != value.size()) {
            throw std::invalid_argument("文件大小必须是非负整数，单位为字节");
        }
        if (parsed > std::numeric_limits<std::uintmax_t>::max()) {
            throw std::out_of_range("file size is too large");
        }
        return static_cast<std::uintmax_t>(parsed);
    } catch (const std::invalid_argument&) {
        throw std::invalid_argument("文件大小必须是非负整数，单位为字节");
    } catch (const std::out_of_range&) {
        throw std::invalid_argument("文件大小超出可表示范围");
    }
}

template <typename String>
std::set<std::string> parse_extensions_impl(const String& value) {
    std::set<std::string> extensions;
    std::basic_istringstream<typename String::value_type> stream(value);
    String item;
    while (std::getline(stream, item, static_cast<typename String::value_type>(','))) {
        std::string narrow;
        narrow.reserve(item.size());
        for (const auto character : item) {
            if (static_cast<unsigned long long>(character) > 127) {
                throw std::invalid_argument("扩展名过滤器目前只支持 ASCII 扩展名");
            }
            narrow.push_back(static_cast<char>(character));
        }
        const std::string normalized = normalize_extension(std::move(narrow));
        if (!normalized.empty()) {
            extensions.insert(normalized);
        }
    }
    return extensions;
}

}  // namespace

std::string display_path(const fs::path& path) {
    try {
        return path.u8string();
    } catch (const std::exception&) {
        return "<无法显示的路径>";
    }
}

std::set<std::string> parse_extensions(const std::string& value) {
    return parse_extensions_impl(value);
}

std::optional<std::uintmax_t> parse_size_bytes(const std::string& value) {
    return parse_size_bytes_impl(value);
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
std::set<std::string> parse_extensions(const std::wstring& value) {
    return parse_extensions_impl(value);
}

std::optional<std::uintmax_t> parse_size_bytes(const std::wstring& value) {
    return parse_size_bytes_impl(value);
}

std::size_t parse_worker_count(const std::wstring& value) {
    return parse_worker_count_impl(value);
}
#endif

ScanResult scan_directory(const ScanOptions& options) {
    const auto throw_scan_error = [&](const std::string& message) {
        if (options.error_logger) {
            options.error_logger->log(display_path(options.root), message);
        }
        throw std::invalid_argument(message);
    };

    if (options.worker_count == 0 || options.worker_count > 64) {
        throw_scan_error("线程数必须是 1 到 64 之间的整数");
    }

    std::error_code root_error;
    if (!fs::exists(options.root, root_error) || root_error) {
        throw_scan_error("目录不存在或无法访问: " + display_path(options.root));
    }
    if (!fs::is_directory(options.root, root_error) || root_error) {
        throw_scan_error("目标不是目录: " + display_path(options.root));
    }

    ScanStats stats;
    const std::size_t queue_capacity =
        std::max<std::size_t>(options.worker_count * 8, 64);
    ThreadPool thread_pool(options.worker_count, queue_capacity);
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
            stats.record_error(options.root,
                               "目录遍历失败: " + iterator_error.message(),
                               options.error_logger);
            iterator_error.clear();
            continue;
        }

        std::error_code entry_error;
        if (it->is_regular_file(entry_error)) {
            fs::path file;
            try {
                file = it->path();
                const std::string extension = extension_for_path(file);
                if (!options.filter.extensions.empty() &&
                    options.filter.extensions.find(extension) ==
                        options.filter.extensions.end()) {
                    continue;
                }
                if (options.discovered_files) {
                    options.discovered_files->fetch_add(1, std::memory_order_relaxed);
                }

                const auto minimum_size = options.filter.min_size;
                const auto maximum_size = options.filter.max_size;
                thread_pool.enqueue_with_cancel(options.cancel,
                                                [file = std::move(file),
                                                 extension,
                                                 minimum_size,
                                                 maximum_size,
                                                 &stats,
                                                 cancel = options.cancel,
                                                 error_logger = options.error_logger] {
                    if (cancel && cancel->load(std::memory_order_relaxed)) {
                        return;
                    }

                    std::error_code file_error;
                    const std::uintmax_t size = fs::file_size(file, file_error);
                    if (file_error) {
                        stats.record_error(file,
                                           "读取文件大小失败: " + file_error.message(),
                                           error_logger);
                        return;
                    }
                    if ((minimum_size && size < *minimum_size) ||
                        (maximum_size && size > *maximum_size)) {
                        return;
                    }
                    stats.record_file(file, size, extension);
                });
            } catch (const std::exception& error) {
                if (options.cancel && options.cancel->load(std::memory_order_relaxed)) {
                    cancelled = true;
                    break;
                }
                stats.record_error(file,
                                   "提交文件任务失败: " + std::string(error.what()),
                                   options.error_logger);
            }
        } else if (entry_error) {
            stats.record_error(it->path(),
                               "读取目录项失败: " + entry_error.message(),
                               options.error_logger);
        }
    }

    if (iterator_error) {
        stats.record_error(options.root,
                           "目录遍历失败: " + iterator_error.message(),
                           options.error_logger);
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
    result.files = std::move(stats.files);
    result.errors = std::move(stats.errors);
    result.log_write_failures = options.error_logger
                                    ? options.error_logger->write_failure_count()
                                    : 0;
    return result;
}

}  // namespace scanner
