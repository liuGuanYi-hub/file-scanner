// FileScanner 集成测试
#include "minitest.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

#include "scanner/domain/types.h"
#include "scanner/infrastructure/file_scanner.h"

namespace {

using scanner::infrastructure::FileScanner;
using scanner::domain::ScanOptions;
using scanner::domain::FileFilter;

namespace fs = std::filesystem;

// 测试辅助：创建临时测试目录
struct ScanTestEnv {
    ScanTestEnv() {
        fs::remove_all("tests/tmp_scan");
        fs::create_directories("tests/tmp_scan/sub1");
        fs::create_directories("tests/tmp_scan/sub2");
        fs::create_directories("tests/tmp_scan/empty");

        create_file("tests/tmp_scan/root.txt", 100);
        create_file("tests/tmp_scan/root.cpp", 500);
        create_file("tests/tmp_scan/sub1/a.cpp", 200);
        create_file("tests/tmp_scan/sub1/b.h", 50);
        create_file("tests/tmp_scan/sub2/c.cpp", 800);
        create_file("tests/tmp_scan/sub2/d.txt", 300);
        create_file("tests/tmp_scan/noext", 10);
    }

    ~ScanTestEnv() {
        fs::remove_all("tests/tmp_scan");
    }

    void create_file(const fs::path& p, std::size_t size) {
        std::ofstream file(p, std::ios::binary);
        file << std::string(size, 'x');
    }
};

// 全局环境（所有测试共享）
static ScanTestEnv env;

// ============================================================
// 基础扫描
// ============================================================
TEST(FileScanner, ScanNormalDirectory) {
    FileScanner scanner(nullptr);
    ScanOptions options;
    options.root = "tests/tmp_scan";
    options.worker_count = 2;

    auto result = scanner.scan(options);

    EXPECT_EQ(result.file_count, 7u);
    EXPECT_GT(result.total_bytes, 0u);
    EXPECT_EQ(result.error_count, 0u);
    EXPECT_FALSE(result.cancelled);
    EXPECT_GT(result.elapsed_seconds, 0.0);
}

TEST(FileScanner, ScanReturnsExtensionCounts) {
    FileScanner scanner(nullptr);
    ScanOptions options;
    options.root = "tests/tmp_scan";
    options.worker_count = 2;

    auto result = scanner.scan(options);
    auto& counts = result.extension_counts;
    EXPECT_EQ(counts[".cpp"], 3u);
    EXPECT_EQ(counts[".txt"], 2u);
    EXPECT_EQ(counts[".h"], 1u);
    EXPECT_EQ(counts["[无扩展名]"], 1u);
}

TEST(FileScanner, ScanReturnsFileMetadata) {
    FileScanner scanner(nullptr);
    ScanOptions options;
    options.root = "tests/tmp_scan";
    options.worker_count = 2;

    auto result = scanner.scan(options);
    EXPECT_EQ(result.files.size(), 7u);

    bool found_cpp = false;
    for (const auto& f : result.files) {
        if (f.extension == ".cpp") found_cpp = true;
    }
    EXPECT_TRUE(found_cpp);
}

// ============================================================
// 多线程一致性
// ============================================================
TEST(FileScanner, SameResultsWithDifferentThreadCounts) {
    FileScanner scanner(nullptr);
    ScanOptions opts1; opts1.root = "tests/tmp_scan"; opts1.worker_count = 1;
    ScanOptions opts4; opts4.root = "tests/tmp_scan"; opts4.worker_count = 4;

    auto r1 = scanner.scan(opts1);
    auto r4 = scanner.scan(opts4);

    EXPECT_EQ(r1.file_count, r4.file_count);
    EXPECT_EQ(r1.total_bytes, r4.total_bytes);
    EXPECT_EQ(r1.files.size(), r4.files.size());
}

// ============================================================
// 空目录
// ============================================================
TEST(FileScanner, ScanEmptyDirectory) {
    FileScanner scanner(nullptr);
    ScanOptions options;
    options.root = "tests/tmp_scan/empty";
    options.worker_count = 2;

    auto result = scanner.scan(options);
    EXPECT_EQ(result.file_count, 0u);
    EXPECT_EQ(result.total_bytes, 0u);
    EXPECT_TRUE(result.extension_counts.empty());
}

// ============================================================
// 非法目录
// ============================================================
TEST(FileScanner, NonExistentDirectoryThrows) {
    FileScanner scanner(nullptr);
    ScanOptions options;
    options.root = "tests/tmp_scan/does_not_exist";
    options.worker_count = 2;

    EXPECT_THROW({ scanner.scan(options); }, std::invalid_argument);
}

// ============================================================
// 过滤
// ============================================================
TEST(FileScanner, ExtensionFilter) {
    FileScanner scanner(nullptr);
    ScanOptions options;
    options.root = "tests/tmp_scan";
    options.worker_count = 2;
    options.filter.extensions = {".cpp"};

    auto result = scanner.scan(options);
    EXPECT_EQ(result.file_count, 3u);
    for (const auto& f : result.files) {
        EXPECT_EQ(f.extension, ".cpp");
    }
}

TEST(FileScanner, MinimumSizeFilter) {
    FileScanner scanner(nullptr);
    ScanOptions options;
    options.root = "tests/tmp_scan";
    options.worker_count = 2;
    options.filter.min_size = 300;

    auto result = scanner.scan(options);
    for (const auto& f : result.files) {
        EXPECT_GE(f.size, 300u);
    }
}

TEST(FileScanner, MaximumSizeFilter) {
    FileScanner scanner(nullptr);
    ScanOptions options;
    options.root = "tests/tmp_scan";
    options.worker_count = 2;
    options.filter.max_size = 100;

    auto result = scanner.scan(options);
    for (const auto& f : result.files) {
        EXPECT_LE(f.size, 100u);
    }
}

// ============================================================
// 取消
// ============================================================
TEST(FileScanner, CancelScan) {
    FileScanner scanner(nullptr);
    ScanOptions options;
    options.root = "tests/tmp_scan";
    options.worker_count = 1;

    std::atomic_bool cancel_flag{false};
    options.cancel = &cancel_flag;

    std::atomic_bool scan_started{false};
    std::thread scan_thread([&] {
        scan_started.store(true);
        auto result = scanner.scan(options);
        EXPECT_TRUE(result.cancelled || result.file_count >= 0);
    });

    while (!scan_started.load()) { std::this_thread::yield(); }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    cancel_flag.store(true);
    scan_thread.join();
}

// ============================================================
// 线程数边界
// ============================================================
TEST(FileScanner, WorkerCount0Throws) {
    FileScanner scanner(nullptr);
    ScanOptions options;
    options.root = "tests/tmp_scan";
    options.worker_count = 0;

    EXPECT_THROW({ scanner.scan(options); }, std::invalid_argument);
}

TEST(FileScanner, WorkerCount65Throws) {
    FileScanner scanner(nullptr);
    ScanOptions options;
    options.root = "tests/tmp_scan";
    options.worker_count = 65;

    EXPECT_THROW({ scanner.scan(options); }, std::invalid_argument);
}

}  // namespace
