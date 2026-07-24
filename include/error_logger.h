#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace scanner {

class ErrorLogger {
public:
    explicit ErrorLogger(const std::filesystem::path& log_path);
    ~ErrorLogger();

    ErrorLogger(const ErrorLogger&) = delete;
    ErrorLogger& operator=(const ErrorLogger&) = delete;

    void log(const std::string& path, const std::string& message) noexcept;
    const std::filesystem::path& path() const noexcept;
    std::uint64_t write_failure_count() const noexcept;

private:
    std::filesystem::path path_;
    std::ofstream stream_;
    mutable std::mutex mutex_;
    std::uint64_t write_failure_count_ = 0;
};

}  // namespace scanner
