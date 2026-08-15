// DuplicateFinder 单元测试（两种模式）
#include "minitest.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "scanner/infrastructure/duplicate_finder.h"

namespace {

using scanner::infrastructure::DuplicateFinder;
using scanner::infrastructure::DuplicateGroup;
using scanner::infrastructure::DuplicateMode;
namespace fs = std::filesystem;

struct DupTestEnv {
    DupTestEnv() {
        fs::remove_all("tests/tmp_dupes");
        fs::create_directories("tests/tmp_dupes/dir1");
        fs::create_directories("tests/tmp_dupes/dir2");

        // 同内容同文件名（散落两目录）
        write("tests/tmp_dupes/dir1/same.txt", "hello hello hello");
        write("tests/tmp_dupes/dir2/same.txt", "hello hello hello");

        // 同内容不同文件名
        write("tests/tmp_dupes/dir1/renamed_a.dat", "unique content 123");
        write("tests/tmp_dupes/dir2/renamed_b.dat", "unique content 123");

        // 不同内容
        write("tests/tmp_dupes/dir1/other.txt", "something else");
        write("tests/tmp_dupes/dir2/other.txt", "completely different");
    }

    ~DupTestEnv() {
        fs::remove_all("tests/tmp_dupes");
    }

    void write(const fs::path& p, const std::string& content) {
        std::ofstream f(p, std::ios::binary);
        f << content;
    }
};

static DupTestEnv env;

TEST(DuplicateFinder, ContentModeFindsSameNameAndRenamed) {
    DuplicateFinder finder;
    auto groups = finder.find("tests/tmp_dupes", 2, DuplicateMode::Content);

    // 期望 2 组：
    //  - same.txt × 2（同名）
    //  - renamed_a.dat / renamed_b.dat（改名）
    bool found_same_name = false;
    bool found_renamed = false;
    for (const auto& g : groups) {
        if (g.paths.size() != 2) continue;
        std::string joined;
        for (const auto& p : g.paths) joined += p;
        if (joined.find("same.txt") != std::string::npos) found_same_name = true;
        if (joined.find("renamed_a.dat") != std::string::npos) found_renamed = true;
    }
    EXPECT_TRUE(found_same_name);
    EXPECT_TRUE(found_renamed);
}

TEST(DuplicateFinder, NameAndSizeModeFindsSameNameOnly) {
    DuplicateFinder finder;
    auto groups = finder.find("tests/tmp_dupes", 2, DuplicateMode::NameAndSize);

    // 只应找到 same.txt（同名同内容）
    // renamed_a/b 虽然内容相同，但文件名不同，NameAndSize 模式不认为重复
    bool found_same_name = false;
    for (const auto& g : groups) {
        std::string joined;
        for (const auto& p : g.paths) joined += p;
        if (joined.find("same.txt") != std::string::npos) found_same_name = true;
        EXPECT_FALSE(joined.find("renamed") != std::string::npos);
    }
    EXPECT_TRUE(found_same_name);
}

TEST(DuplicateFinder, ContentModeDoesNotMixDifferentContent) {
    DuplicateFinder finder;
    auto groups = finder.find("tests/tmp_dupes", 2, DuplicateMode::Content);
    for (const auto& g : groups) {
        for (const auto& p : g.paths) {
            // other.txt 的两个版本内容不同，绝不能被误判为重复
            EXPECT_TRUE(p.find("other.txt") == std::string::npos);
        }
    }
}

TEST(DuplicateFinder, NoDuplicatesInEmptyDir) {
    fs::create_directories("tests/tmp_dupes/empty_only");
    DuplicateFinder finder;
    auto groups = finder.find("tests/tmp_dupes/empty_only", 1, DuplicateMode::Content);
    EXPECT_TRUE(groups.empty());
}

}  // namespace
