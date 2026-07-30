#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include "scanner/application/i_logger.h"

namespace scanner {
namespace infrastructure {

// === 错误日志记录器 ===
// 实现 ILogger 接口，线程安全地将错误写入 UTF-8 文件
class ErrorLogger : public application::ILogger {
public:
    explicit ErrorLogger(const std::filesystem::path& log_path);
    ~ErrorLogger() override;

    ErrorLogger(const ErrorLogger&) = delete;
    ErrorLogger& operator=(const ErrorLogger&) = delete;

    void log(const std::string& path,
             const std::string& message) noexcept override;
    const std::filesystem::path& path() const noexcept;
    std::uint64_t failure_count() const noexcept override;

private:
    std::filesystem::path path_;
    std::ofstream stream_;
    mutable std::mutex mutex_;
    std::uint64_t failure_count_ = 0;
};

}  // namespace infrastructure
}  // namespace scanner
