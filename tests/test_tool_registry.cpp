// ToolRegistry 插件化工具测试
#include "minitest.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "scanner/application/tool_registry.h"
#include "scanner/infrastructure/tool_factories.h"

namespace {

using scanner::application::ToolRegistry;
using scanner::application::ToolContext;
using scanner::infrastructure::make_scan_tool;
using scanner::infrastructure::make_duplicates_tool;
using scanner::infrastructure::make_big_files_tool;
namespace fs = std::filesystem;

struct ToolTestEnv {
    ToolTestEnv() {
        fs::remove_all("tests/tmp_tools");
        fs::create_directories("tests/tmp_tools");
        std::ofstream f1("tests/tmp_tools/a.txt");
        f1 << std::string(100, 'a');
        std::ofstream f2("tests/tmp_tools/big.bin", std::ios::binary);
        f2 << std::string(2000, 'b');  // 2KB，超过 1024 阈值
    }
    ~ToolTestEnv() { fs::remove_all("tests/tmp_tools"); }
};

static ToolTestEnv env;

TEST(ToolRegistry, RegisterAndGetByName) {
    ToolRegistry registry;
    registry.register_tool(make_scan_tool());
    registry.register_tool(make_duplicates_tool());
    registry.register_tool(make_big_files_tool(1024));

    EXPECT_EQ(static_cast<int>(registry.size()), 3);
    EXPECT_TRUE(registry.contains("scan"));
    EXPECT_TRUE(registry.contains("dupes"));
    EXPECT_TRUE(registry.contains("big-files"));
    EXPECT_FALSE(registry.contains("nope"));

    auto names = registry.names();
    EXPECT_EQ(static_cast<int>(names.size()), 3);
}

TEST(ToolRegistry, MissingToolReturnsNull) {
    ToolRegistry registry;
    EXPECT_EQ(registry.get("not-exist"), nullptr);
}

TEST(ToolRegistry, RunScanTool) {
    ToolRegistry registry;
    registry.register_tool(make_scan_tool());
    auto tool = registry.get("scan");
    ASSERT_TRUE(tool != nullptr);

    ToolContext ctx;
    ctx.root = "tests/tmp_tools";
    ctx.worker_count = 2;
    auto out = tool->run(ctx);
    EXPECT_EQ(out.exit_code, 0);
    EXPECT_FALSE(out.summary.empty());
    // 摘要应包含"文件数"
    EXPECT_TRUE(out.summary.find("文件数") != std::string::npos);
}

TEST(ToolRegistry, RunBigFilesTool) {
    ToolRegistry registry;
    registry.register_tool(make_big_files_tool(1024));
    auto tool = registry.get("big-files");
    ASSERT_TRUE(tool != nullptr);

    ToolContext ctx;
    ctx.root = "tests/tmp_tools";
    auto out = tool->run(ctx);
    EXPECT_EQ(out.exit_code, 0);
    // 只有 big.bin (2KB) 超过 1024B 阈值
    EXPECT_EQ(static_cast<int>(out.items.size()), 1);
    EXPECT_TRUE(out.items[0].find("big.bin") != std::string::npos);
}

TEST(ToolRegistry, RunDupesTool) {
    // 构造两个相同内容的文件
    std::ofstream f1("tests/tmp_tools/dup1.txt");
    f1 << "identical content";
    std::ofstream f2("tests/tmp_tools/dup2.txt");
    f2 << "identical content";

    ToolRegistry registry;
    registry.register_tool(make_duplicates_tool());
    auto tool = registry.get("dupes");
    ASSERT_TRUE(tool != nullptr);

    ToolContext ctx;
    ctx.root = "tests/tmp_tools";
    auto out = tool->run(ctx);
    EXPECT_EQ(out.exit_code, 0);
    EXPECT_FALSE(out.items.empty());  // 至少找到 dup1/dup2 一组
}

}  // namespace
