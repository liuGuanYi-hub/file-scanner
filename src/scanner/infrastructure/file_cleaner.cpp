#ifdef _WIN32

#include "scanner/infrastructure/file_cleaner.h"

#include <windows.h>
#include <shellapi.h>
#include <algorithm>

namespace scanner {
namespace infrastructure {

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

}  // namespace infrastructure
}  // namespace scanner

#endif  // _WIN32
