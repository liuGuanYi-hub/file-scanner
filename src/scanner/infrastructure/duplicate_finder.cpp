#include "scanner/infrastructure/duplicate_finder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <openssl/evp.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace scanner {
namespace infrastructure {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kPartialReadSize = 64 * 1024;  // 64KB

// 字节数组转十六进制字符串
std::string to_hex(const unsigned char* data, std::size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<unsigned int>(data[i]);
    }
    return oss.str();
}

// 从流中读取一段数据并计算 SHA256
std::string compute_sha256(std::ifstream& stream, std::size_t max_bytes) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return {};

    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);

    std::array<char, 8192> buffer{};
    std::size_t total_read = 0;

    while (total_read < max_bytes) {
        std::size_t to_read = std::min(buffer.size(), max_bytes - total_read);
        stream.read(buffer.data(), static_cast<std::streamsize>(to_read));
        std::streamsize bytes_read = stream.gcount();
        if (bytes_read <= 0) break;
        EVP_DigestUpdate(ctx, buffer.data(),
                         static_cast<std::size_t>(bytes_read));
        total_read += static_cast<std::size_t>(bytes_read);
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);

    return to_hex(hash, hash_len);
}

}  // namespace

std::string DuplicateFinder::hash_file(const fs::path& path, bool partial) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }

    if (partial) {
        return compute_sha256(file, kPartialReadSize);
    } else {
        return compute_sha256(file, std::numeric_limits<std::size_t>::max());
    }
}

std::vector<DuplicateGroup> DuplicateFinder::find(
    const fs::path& root, std::size_t worker_count, DuplicateMode mode) {
    if (worker_count == 0 || worker_count > 64) {
        throw std::invalid_argument("线程数必须是 1 到 64 之间的整数");
    }

    // ============================================================
    // 第 1 步：遍历目录，按 大小（Content）或 文件名+大小（NameAndSize）分组
    // ============================================================
    // Content:    size -> {path, path, ...}
    // NameAndSize: (filename, size) -> {path, path, ...}  （零 I/O 预筛）
    std::map<std::pair<std::string, std::uintmax_t>, std::vector<fs::path>> name_size_groups;
    std::map<std::uintmax_t, std::vector<fs::path>> size_groups;

    std::error_code ec;
    for (fs::recursive_directory_iterator it(
             root, fs::directory_options::skip_permission_denied, ec),
         end;
         it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code entry_ec;
        if (it->is_regular_file(entry_ec)) {
            std::error_code size_ec;
            auto size = fs::file_size(it->path(), size_ec);
            if (size_ec) continue;
            if (mode == DuplicateMode::NameAndSize) {
                std::string name = it->path().filename().u8string();
                name_size_groups[{name, size}].push_back(it->path());
            } else {
                size_groups[size].push_back(it->path());
            }
        }
    }

    // 统一转成候选组集合：每组内文件数 >= 2 才需要进一步哈希
    std::vector<std::vector<fs::path>> candidate_groups;
    if (mode == DuplicateMode::NameAndSize) {
        for (auto& [key, paths] : name_size_groups) {
            if (paths.size() >= 2) candidate_groups.push_back(std::move(paths));
        }
    } else {
        for (auto& [size, paths] : size_groups) {
            if (paths.size() >= 2) candidate_groups.push_back(std::move(paths));
        }
    }

    // ============================================================
    // 第 2 步：对候选组计算前 64KB 哈希
    // ============================================================
    // partial_hash -> {path, path, ...}
    std::map<std::string, std::vector<fs::path>> partial_hash_groups;

    for (auto& paths : candidate_groups) {
        for (const auto& p : paths) {
            auto hash = hash_file(p, true);
            if (!hash.empty()) {
                partial_hash_groups[hash].push_back(p);
            }
        }
    }

    // ============================================================
    // 第 3 步：对同哈希的文件组计算完整文件哈希
    // ============================================================
    std::vector<DuplicateGroup> results;

    for (auto& [partial_hash, paths] : partial_hash_groups) {
        if (paths.size() < 2) continue;

        // 完整文件哈希 -> 路径
        std::map<std::string, std::vector<fs::path>> full_hash_groups;
        for (const auto& p : paths) {
            auto hash = hash_file(p, false);
            if (!hash.empty()) {
                full_hash_groups[hash].push_back(p);
            }
        }

        for (auto& [full_hash, group_paths] : full_hash_groups) {
            if (group_paths.size() < 2) continue;

            DuplicateGroup group;
            group.hash = full_hash;
            group.file_size = fs::file_size(group_paths[0]);

            for (const auto& p : group_paths) {
                group.paths.push_back(p.u8string());
            }
            results.push_back(std::move(group));
        }
    }

    return results;
}

}  // namespace infrastructure
}  // namespace scanner
