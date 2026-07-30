#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace scanner {
namespace domain {

namespace fs = std::filesystem;

// === 过滤条件 ===
struct FileFilter {
    std::set<std::string> extensions;
    std::optional<std::uintmax_t> min_size;
    std::optional<std::uintmax_t> max_size;
};

// === 文件元数据 ===
struct FileMetadata {
    std::string path;
    std::uintmax_t size = 0;
    std::string extension;
};

// === 扫描配置 ===
// 注意: Domain 层不依赖 Application 层，日志等基础设施通过构造函数注入
struct ScanOptions {
    fs::path root;
    std::size_t worker_count = 1;
    std::atomic_bool* cancel = nullptr;
    std::atomic<std::uint64_t>* discovered_files = nullptr;
    FileFilter filter;
};

// === 扫描错误 ===
struct ScanError {
    std::string path;
    std::string message;
};

// === 扫描结果 ===
struct ScanResult {
    std::uint64_t file_count = 0;
    std::uint64_t total_bytes = 0;
    std::uint64_t error_count = 0;
    std::uint64_t log_write_failures = 0;
    double elapsed_seconds = 0.0;
    bool cancelled = false;
    std::map<std::string, std::uint64_t> extension_counts;
    std::vector<FileMetadata> files;
    std::vector<ScanError> errors;
};

}  // namespace domain
}  // namespace scanner
