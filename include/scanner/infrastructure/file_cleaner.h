#pragma once

#ifdef _WIN32

#include <string>
#include <vector>

namespace scanner {
namespace infrastructure {

// === 文件清理操作 ===
class FileCleaner {
public:
    // 发送文件到回收站（可恢复）
    // 返回成功数量
    static int recycle(const std::vector<std::string>& paths);

    // 永久删除文件（不可恢复）
    // 返回成功数量
    static int remove_files(const std::vector<std::string>& paths);
};

}  // namespace infrastructure
}  // namespace scanner

#endif  // _WIN32
