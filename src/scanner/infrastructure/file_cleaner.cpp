#ifdef _WIN32

#include "scanner/infrastructure/file_cleaner.h"

#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <filesystem>
#include <tuple>

namespace scanner {
namespace infrastructure {

namespace fs = std::filesystem;

int FileCleaner::recycle(const std::vector<std::string>& paths) {
    int success = 0;
    for (const auto& path : paths) {
        // 转换为宽字符
        std::wstring wpath;
        wpath.reserve(path.size() + 2);
        for (char c : path) wpath.push_back(static_cast<wchar_t>(c));
        wpath.push_back(L'\0');  // 双 null 结尾

        SHFILEOPSTRUCTW op{};
        op.wFunc = FO_DELETE;
        op.pFrom = wpath.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        op.fAnyOperationsAborted = FALSE;

        if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted) {
            ++success;
        }
    }
    return success;
}

int FileCleaner::remove_files(const std::vector<std::string>& paths) {
    int success = 0;
    for (const auto& path : paths) {
        std::wstring wpath;
        wpath.reserve(path.size() + 2);
        for (char c : path) wpath.push_back(static_cast<wchar_t>(c));
        wpath.push_back(L'\0');

        SHFILEOPSTRUCTW op{};
        op.wFunc = FO_DELETE;
        op.pFrom = wpath.c_str();
        op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        op.fAnyOperationsAborted = FALSE;

        if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted) {
            ++success;
        }
    }
    return success;
}

// 路径是否位于任一保护目录下（前缀匹配，目录分隔符规范化）
bool is_protected(const std::string& path,
                  const std::vector<std::string>& protected_dirs) {
    if (protected_dirs.empty()) return false;
    for (const auto& dir : protected_dirs) {
        if (dir.empty()) continue;
        if (path.size() >= dir.size() &&
            path.compare(0, dir.size(), dir) == 0) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> FileCleaner::select_to_delete(
    const std::vector<std::string>& group_paths,
    DeleteStrategy strategy,
    const std::vector<std::string>& protected_dirs) {
    if (group_paths.size() <= 1) return {};  // 无重复可删

    // 1. 剔除受保护文件，剩下的才参与删除
    std::vector<std::string> candidates;
    for (const auto& p : group_paths) {
        if (!is_protected(p, protected_dirs)) candidates.push_back(p);
    }
    if (candidates.size() <= 1) return {};  // 全部受保护或只剩一个

    if (strategy == DeleteStrategy::DeleteAll) return candidates;

    // 2. 找到"要保留"的那个文件：按策略排序取第一个
    auto key = [strategy](const std::string& p) {
        std::error_code ec;
        if (strategy == DeleteStrategy::KeepBiggest ||
            strategy == DeleteStrategy::KeepSmallest) {
            auto size = fs::file_size(p, ec);
            if (ec) size = 0;
            return std::tuple<std::uintmax_t, fs::file_time_type>{
                size, fs::file_time_type::min()};
        }
        auto mtime = fs::last_write_time(p, ec);
        if (ec) mtime = fs::file_time_type::min();
        return std::tuple<std::uintmax_t, fs::file_time_type>{0, mtime};
    };
    // 升序排列：KeepOldest/KeepSmallest 保留第一个；KeepNewest/KeepBiggest 保留最后一个
    std::vector<std::string> sorted = candidates;
    std::sort(sorted.begin(), sorted.end(),
              [&](const std::string& a, const std::string& b) {
                  return key(a) < key(b);
              });

    bool keep_first = (strategy == DeleteStrategy::KeepOldest ||
                       strategy == DeleteStrategy::KeepSmallest);
    const std::string& keep = keep_first ? sorted.front() : sorted.back();

    // 3. 其余全部删除
    std::vector<std::string> to_delete;
    for (const auto& p : candidates) {
        if (p != keep) to_delete.push_back(p);
    }
    return to_delete;
}

}  // namespace infrastructure
}  // namespace scanner

#endif  // _WIN32
