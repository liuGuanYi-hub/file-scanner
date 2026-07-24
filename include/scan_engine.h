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

namespace fs = std::filesystem;

class ErrorLogger;

struct FileFilter {
    std::set<std::string> extensions;
    std::optional<std::uintmax_t> min_size;
    std::optional<std::uintmax_t> max_size;
};

struct FileMetadata {
    std::string path;
    std::uintmax_t size = 0;
    std::string extension;
};

struct ScanOptions {
    fs::path root;
    std::size_t worker_count = 1;
    std::atomic_bool* cancel = nullptr;
    std::atomic<std::uint64_t>* discovered_files = nullptr;
    ErrorLogger* error_logger = nullptr;
    FileFilter filter;
};

struct ScanError {
    std::string path;
    std::string message;
};

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

std::string display_path(const fs::path& path);
std::set<std::string> parse_extensions(const std::string& value);
std::optional<std::uintmax_t> parse_size_bytes(const std::string& value);
std::size_t default_worker_count();
std::size_t parse_worker_count(const std::string& value);

#ifdef _WIN32
std::set<std::string> parse_extensions(const std::wstring& value);
std::optional<std::uintmax_t> parse_size_bytes(const std::wstring& value);
std::size_t parse_worker_count(const std::wstring& value);
#endif

ScanResult scan_directory(const ScanOptions& options);

}  // namespace scanner
