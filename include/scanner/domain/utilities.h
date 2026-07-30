#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>

namespace scanner {
namespace domain {

// === 路径显示 ===
std::string display_path(const std::filesystem::path& path);

// === 扩展名工具 ===
std::string normalize_extension(std::string extension);
std::string extension_for_path(const std::filesystem::path& path);
std::set<std::string> parse_extensions(const std::string& value);

// === 文件大小解析 ===
std::optional<std::uintmax_t> parse_size_bytes(const std::string& value);

// === 线程数工具 ===
std::size_t default_worker_count();
std::size_t parse_worker_count(const std::string& value);

// === 字符串工具 ===
std::string trim_ascii(std::string value);
std::string lowercase_ascii(std::string value);

// === Windows 宽字符重载 ===
#ifdef _WIN32
std::set<std::string> parse_extensions(const std::wstring& value);
std::optional<std::uintmax_t> parse_size_bytes(const std::wstring& value);
std::size_t parse_worker_count(const std::wstring& value);
#endif

}  // namespace domain
}  // namespace scanner
