// FileCleaner 删除策略单元测试
#include "minitest.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "scanner/infrastructure/file_cleaner.h"

#ifdef _WIN32

namespace {

using scanner::infrastructure::FileCleaner;
using scanner::infrastructure::DeleteStrategy;
namespace fs = std::filesystem;

struct CleanerTestEnv {
    CleanerTestEnv() {
        fs::remove_all("tests/tmp_cleaner");
        fs::create_directories("tests/tmp_cleaner");
        fs::create_directories("tests/tmp_cleaner/protected");

        write("tests/tmp_cleaner/a.txt", std::string(100, 'a'));   // 100B
        write("tests/tmp_cleaner/b.txt", std::string(200, 'b'));   // 200B
        write("tests/tmp_cleaner/c.txt", std::string(300, 'c'));   // 300B
        write("tests/tmp_cleaner/protected/p.txt", std::string(50, 'p'));
    }

    ~CleanerTestEnv() {
        fs::remove_all("tests/tmp_cleaner");
    }

    void write(const fs::path& p, const std::string& content) {
        std::ofstream f(p, std::ios::binary);
        f << content;
    }

    std::vector<std::string> paths() const {
        return {"tests/tmp_cleaner/a.txt",
                "tests/tmp_cleaner/b.txt",
                "tests/tmp_cleaner/c.txt"};
    }
};

static CleanerTestEnv env;

TEST(FileCleaner, KeepSmallestDeletesAllButSmallest) {
    auto to_delete = FileCleaner::select_to_delete(
        env.paths(), DeleteStrategy::KeepSmallest);
    // 最小是 a.txt(100B)，应删除 b 和 c
    EXPECT_EQ(static_cast<int>(to_delete.size()), 2);
    bool has_b = false, has_c = false, has_a = false;
    for (const auto& p : to_delete) {
        if (p.find("b.txt") != std::string::npos) has_b = true;
        if (p.find("c.txt") != std::string::npos) has_c = true;
        if (p.find("a.txt") != std::string::npos) has_a = true;
    }
    EXPECT_TRUE(has_b);
    EXPECT_TRUE(has_c);
    EXPECT_FALSE(has_a);
}

TEST(FileCleaner, KeepBiggestDeletesAllButBiggest) {
    auto to_delete = FileCleaner::select_to_delete(
        env.paths(), DeleteStrategy::KeepBiggest);
    // 最大是 c.txt(300B)，应删除 a 和 b
    EXPECT_EQ(static_cast<int>(to_delete.size()), 2);
    for (const auto& p : to_delete) {
        EXPECT_TRUE(p.find("c.txt") == std::string::npos);
    }
}

TEST(FileCleaner, DeleteAllReturnsAll) {
    auto to_delete = FileCleaner::select_to_delete(
        env.paths(), DeleteStrategy::DeleteAll);
    EXPECT_EQ(static_cast<int>(to_delete.size()), 3);
}

TEST(FileCleaner, ProtectedDirNeverDeleted) {
    std::vector<std::string> all = {"tests/tmp_cleaner/a.txt",
                                    "tests/tmp_cleaner/protected/p.txt"};
    auto to_delete = FileCleaner::select_to_delete(
        all, DeleteStrategy::KeepBiggest,
        {"tests/tmp_cleaner/protected"});
    // p.txt 受保护，a.txt 是唯一候选（不足2个）→ 不删任何文件
    EXPECT_TRUE(to_delete.empty());

    // 受保护 + 两个普通文件：只删普通文件，保护文件永不删
    std::vector<std::string> three = {"tests/tmp_cleaner/a.txt",
                                      "tests/tmp_cleaner/b.txt",
                                      "tests/tmp_cleaner/protected/p.txt"};
    auto to_delete2 = FileCleaner::select_to_delete(
        three, DeleteStrategy::KeepSmallest,
        {"tests/tmp_cleaner/protected"});
    EXPECT_EQ(static_cast<int>(to_delete2.size()), 1);  // 只删 b.txt
    for (const auto& p : to_delete2) {
        EXPECT_TRUE(p.find("protected") == std::string::npos);
        EXPECT_TRUE(p.find("a.txt") == std::string::npos);
    }
}

TEST(FileCleaner, SingleFileNeverDeleted) {
    std::vector<std::string> single = {"tests/tmp_cleaner/a.txt"};
    auto to_delete = FileCleaner::select_to_delete(
        single, DeleteStrategy::DeleteAll);
    EXPECT_TRUE(to_delete.empty());
}

}  // namespace

#endif  // _WIN32
