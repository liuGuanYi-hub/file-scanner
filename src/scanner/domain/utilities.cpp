#include "scanner/domain/utilities.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace scanner {
namespace domain {

namespace fs = std::filesystem;

// === 路径显示 ===
std::string display_path(const fs::path& path) {
    try {
        return path.u8string();
    } catch (const std::exception&) {
        return "<无法显示的路径>";
    }
}

// === 字符串工具 ===
std::string trim_ascii(std::string value) {
    const auto is_space = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), [&](char character) {
                    return !is_space(static_cast<unsigned char>(character));
                }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char character) {
                    return !is_space(static_cast<unsigned char>(character));
                }).base(),
                value.end());
    return value;
}

std::string lowercase_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

// === 扩展名工具 ===
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

namespace {

template <typename String>
std::set<std::string> parse_extensions_impl(const String& value) {
    std::set<std::string> extensions;
    std::basic_istringstream<typename String::value_type> stream(value);
    String item;
    while (std::getline(stream, item,
                        static_cast<typename String::value_type>(','))) {
        std::string narrow;
        narrow.reserve(item.size());
        for (const auto character : item) {
            if (static_cast<unsigned long long>(character) > 127) {
                throw std::invalid_argument(
                    "扩展名过滤器目前只支持 ASCII 扩展名");
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

std::set<std::string> parse_extensions(const std::string& value) {
    return parse_extensions_impl(value);
}

// === 文件大小解析 ===
namespace {

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

}  // namespace

std::optional<std::uintmax_t> parse_size_bytes(const std::string& value) {
    return parse_size_bytes_impl(value);
}

// === 线程数工具 ===
namespace {

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

// === Windows 宽字符重载 ===
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

}  // namespace domain
}  // namespace scanner
