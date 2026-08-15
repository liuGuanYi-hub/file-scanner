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

// === 重复文件检测模式 ===
// 对标 czkawka 的 CheckingMethod（Name/SizeName/Size/Hash）：
//   Content    — 按内容查重（默认）：大小 → 64KB 预哈希 → 完整哈希
//                能找到所有内容相同的文件（含不同文件名）
//   NameAndSize— 快速模式：文件名 + 大小 → 64KB 预哈希 → 完整哈希
//                零 I/O 预筛，适合"同名文件散落在不同目录"的场景
enum class DuplicateMode {
    Content,      // 按内容查重（最精确，默认）
    NameAndSize,  // 按 文件名+大小 查重（最快，可能漏掉改名文件）
};

// === 重复文件检测器 ===
// 三级检测策略：
//   1. 按文件大小分组（或 文件名+大小）
//   2. 相同大小的计算前 64KB 的 SHA256 哈希
//   3. 相同哈希的计算完整文件 SHA256
class DuplicateFinder {
public:
    // 扫描目录中的所有文件，返回重复文件组
    // 每个组包含 2 个或以上相同内容的文件
    //  - cache_db: 可选预哈希缓存数据库路径（SQLite）
    //    缓存 64KB 预哈希中间结果，二次扫描相同文件跳过 I/O；
    //    传 nullptr 表示不启用缓存（向后兼容）
    std::vector<DuplicateGroup> find(
        const std::filesystem::path& root,
        std::size_t worker_count = 1,
        DuplicateMode mode = DuplicateMode::Content,
        const std::filesystem::path* cache_db = nullptr);

private:
    // 计算文件的 SHA256 哈希
    // partial: true 表示只读前 64KB
    std::string hash_file(const std::filesystem::path& path, bool partial);
};

}  // namespace infrastructure
}  // namespace scanner
