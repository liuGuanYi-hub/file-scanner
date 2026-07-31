#pragma once

#ifdef _WIN32

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace scanner {
namespace infrastructure {

// === 文件系统监听器 (Windows) ===
// 使用 ReadDirectoryChangesW 监听目录变化
class FileWatcher {
public:
    enum Event { CREATED, DELETED, MODIFIED, RENAMED_OLD, RENAMED_NEW };

    using Callback = std::function<void(Event, const std::string& path)>;

    FileWatcher();
    ~FileWatcher();

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    // 开始监听目录，回调在独立线程中调用
    void start(const std::string& directory, Callback callback);

    // 停止监听
    void stop();

    // 是否正在监听
    bool watching() const;

private:
    void watch_loop(const std::string& directory, Callback callback);

    std::atomic_bool running_{false};
    std::thread worker_;
};

}  // namespace infrastructure
}  // namespace scanner

#endif  // _WIN32
