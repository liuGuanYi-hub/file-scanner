#include "error_logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace scanner {

namespace {

std::string timestamp_now() {
    const std::time_t current_time = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm local_time{};
#ifdef _WIN32
    localtime_s(&local_time, &current_time);
#else
    localtime_r(&current_time, &local_time);
#endif

    std::ostringstream output;
    output << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

std::string sanitize_field(std::string value) {
    for (char& character : value) {
        if (character == '\r' || character == '\n' || character == '\t') {
            character = ' ';
        }
    }
    return value;
}

}  // namespace

ErrorLogger::ErrorLogger(const std::filesystem::path& log_path) : path_(log_path) {
    if (path_.empty()) {
        throw std::invalid_argument("错误日志路径不能为空");
    }

    std::error_code directory_error;
    const std::filesystem::path parent = path_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, directory_error);
        if (directory_error) {
            throw std::runtime_error("无法创建错误日志目录: " +
                                     directory_error.message());
        }
    }

    stream_.open(path_, std::ios::out | std::ios::app);
    if (!stream_) {
        throw std::runtime_error("无法打开错误日志文件: " + path_.u8string());
    }

    stream_.seekp(0, std::ios::end);
    if (stream_.tellp() == std::streampos(0)) {
        stream_ << "时间\t路径\t错误信息\n";
        stream_.flush();
        if (!stream_) {
            throw std::runtime_error("无法写入错误日志文件: " + path_.u8string());
        }
    }
}

ErrorLogger::~ErrorLogger() {
    std::lock_guard<std::mutex> lock(mutex_);
    stream_.flush();
}

void ErrorLogger::log(const std::string& path, const std::string& message) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!stream_) {
            ++write_failure_count_;
            return;
        }

        stream_ << timestamp_now() << '\t' << sanitize_field(path) << '\t'
                << sanitize_field(message) << '\n';
        stream_.flush();
        if (!stream_) {
            ++write_failure_count_;
            stream_.clear();
        }
    } catch (...) {
        // 日志属于辅助诊断能力，不能让记录失败反过来终止扫描。
    }
}

const std::filesystem::path& ErrorLogger::path() const noexcept {
    return path_;
}

std::uint64_t ErrorLogger::write_failure_count() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return write_failure_count_;
}

}  // namespace scanner
