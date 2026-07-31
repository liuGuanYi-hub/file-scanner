#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace scanner {
namespace infrastructure {

// === 重复文件组 ===
struct DuplicateGroup {
    std::uintmax_t file_size = 0;
    std::string hash;
    std::vector<std::string> paths;
};

// === 重复文件检测器 ===
// 三级检测策略：
//   1. 按文件大小分组
//   2. 相同大小的计算前 64KB 的 SHA256 哈希
//   3. 相同哈希的计算完整文件 SHA256
class DuplicateFinder {
public:
    // 扫描目录中的所有文件，返回重复文件组
    // 每个组包含 2 个或以上相同内容的文件
    std::vector<DuplicateGroup> find(
        const std::filesystem::path& root,
        std::size_t worker_count = 1);

private:
    // 计算文件的 SHA256 哈希
    // partial: true 表示只读前 64KB
    std::string hash_file(const std::filesystem::path& path, bool partial);
};

}  // namespace infrastructure
}  // namespace scanner
