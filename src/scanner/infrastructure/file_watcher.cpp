#ifdef _WIN32

#include "scanner/infrastructure/file_watcher.h"

#include <windows.h>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace scanner {
namespace infrastructure {

namespace {

constexpr std::size_t kBufferSize = 64 * 1024;

std::string wide_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(),
                                  static_cast<int>(ws.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(),
                        static_cast<int>(ws.size()),
                        result.data(), len, nullptr, nullptr);
    return result;
}

FileWatcher::Event action_to_event(DWORD action) {
    switch (action) {
        case FILE_ACTION_ADDED:            return FileWatcher::CREATED;
        case FILE_ACTION_REMOVED:          return FileWatcher::DELETED;
        case FILE_ACTION_MODIFIED:         return FileWatcher::MODIFIED;
        case FILE_ACTION_RENAMED_OLD_NAME: return FileWatcher::RENAMED_OLD;
        case FILE_ACTION_RENAMED_NEW_NAME: return FileWatcher::RENAMED_NEW;
        default: return FileWatcher::MODIFIED;
    }
}

}  // namespace

FileWatcher::FileWatcher() = default;

FileWatcher::~FileWatcher() {
    stop();
}

void FileWatcher::start(const std::string& directory, Callback callback) {
    if (running_.load(std::memory_order_relaxed)) return;
    running_.store(true, std::memory_order_relaxed);
    worker_ = std::thread(&FileWatcher::watch_loop, this, directory,
                          std::move(callback));
}

void FileWatcher::stop() {
    running_.store(false, std::memory_order_relaxed);
    if (worker_.joinable()) worker_.join();
}

bool FileWatcher::watching() const {
    return running_.load(std::memory_order_relaxed);
}

void FileWatcher::watch_loop(const std::string& directory,
                              Callback callback) {
    // 打开目录句柄
    std::wstring wdir;
    wdir.reserve(directory.size() + 2);
    for (char c : directory) wdir.push_back(static_cast<wchar_t>(c));

    HANDLE hDir = CreateFileW(
        wdir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);

    if (hDir == INVALID_HANDLE_VALUE) {
        running_.store(false);
        return;
    }

    std::array<BYTE, kBufferSize> buffer{};
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    while (running_.load(std::memory_order_relaxed)) {
        DWORD bytes_returned = 0;
        BOOL ok = ReadDirectoryChangesW(
            hDir, buffer.data(), kBufferSize,
            TRUE,  // 递归监听子目录
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
            &bytes_returned, &overlapped, nullptr);

        if (!ok) break;

        // 等待变更事件
        DWORD wait = WaitForSingleObject(overlapped.hEvent, 1000);
        if (wait != WAIT_OBJECT_0) continue;

        GetOverlappedResult(hDir, &overlapped, &bytes_returned, FALSE);

        // 解析事件
        BYTE* ptr = buffer.data();
        while (ptr) {
            auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(ptr);
            std::wstring name(info->FileName,
                              info->FileNameLength / sizeof(wchar_t));
            std::string path = directory + "/" + wide_to_utf8(name);
            callback(action_to_event(info->Action), path);

            if (info->NextEntryOffset == 0) break;
            ptr += info->NextEntryOffset;
        }

        ResetEvent(overlapped.hEvent);
    }

    CloseHandle(overlapped.hEvent);
    CloseHandle(hDir);
    running_.store(false);
}

}  // namespace infrastructure
}  // namespace scanner

#endif  // _WIN32
