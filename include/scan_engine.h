#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace scanner {

namespace fs = std::filesystem;

struct ScanOptions {
    fs::path root;
    std::size_t worker_count = 1;
    std::atomic_bool* cancel = nullptr;
    std::atomic<std::uint64_t>* discovered_files = nullptr;
};

struct ScanResult {
    std::uint64_t file_count = 0;
    std::uint64_t total_bytes = 0;
    std::uint64_t error_count = 0;
    double elapsed_seconds = 0.0;
    bool cancelled = false;
    std::map<std::string, std::uint64_t> extension_counts;
};

std::string display_path(const fs::path& path);
std::size_t default_worker_count();
std::size_t parse_worker_count(const std::string& value);

#ifdef _WIN32
std::size_t parse_worker_count(const std::wstring& value);
#endif

ScanResult scan_directory(const ScanOptions& options);

}  // namespace scanner
