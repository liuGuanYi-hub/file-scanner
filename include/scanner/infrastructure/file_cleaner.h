#pragma once

#ifdef _WIN32

#include <string>
#include <vector>

namespace scanner {
namespace infrastructure {

// === 删除策略（对标 czkawka DeleteMethod） ===
enum class DeleteStrategy {
    KeepNewest,     // 保留最新修改的文件，删除其余
    KeepOldest,     // 保留最旧修改的文件，删除其余
    KeepBiggest,    // 保留最大的文件，删除其余
    KeepSmallest,   // 保留最小的文件，删除其余
    DeleteAll,      // 全部删除
};

// === 文件清理操作 ===
class FileCleaner {
public:
    // 发送文件到回收站（可恢复）
    // 返回成功数量
    static int recycle(const std::vector<std::string>& paths);

    // 永久删除文件（不可恢复）
    // 返回成功数量
    static int remove_files(const std::vector<std::string>& paths);

    // 按删除策略从一组路径中选出"应删除"的子集（纯逻辑，不执行删除）
    //  - 每组至少保留一个文件（DeleteAll 除外）
    //  - protected_dirs 中列出的目录（前缀匹配）下的文件永不删除
    //  - 返回空表示全部保留
    static std::vector<std::string> select_to_delete(
        const std::vector<std::string>& group_paths,
        DeleteStrategy strategy,
        const std::vector<std::string>& protected_dirs = {});
};

}  // namespace infrastructure
}  // namespace scanner

#endif  // _WIN32
