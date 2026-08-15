#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace scanner {
namespace application {

// === 工具执行上下文 ===
// 统一传入各扫描/清理工具所需的公共参数
// 各工具可忽略不关心的字段（默认值）
struct ToolContext {
    std::filesystem::path root;      // 扫描根目录
    std::size_t worker_count = 1;    // 线程数
    std::string extra;               // 工具自定义参数（如策略/模式）
    bool dry_run = false;            // 预览模式（不实际执行删除等）
};

// === 工具执行结果 ===
struct ToolOutput {
    std::string summary;             // 人类可读摘要
    std::vector<std::string> items;  // 结果明细（路径/说明）
    int exit_code = 0;
};

// === 扫描/清理工具统一接口（对标 czkawka tools/ 的 trait 抽象） ===
// 新增工具只需实现本接口并在注册表中注册，CLI/GUI 无需改动
class IScanTool {
public:
    virtual ~IScanTool() = default;

    // 工具唯一名称（注册表键），如 "scan" / "dupes" / "big-files"
    virtual std::string name() const = 0;

    // 一句话说明（用于 --tools 列表）
    virtual std::string description() const = 0;

    // 执行工具
    virtual ToolOutput run(const ToolContext& ctx) = 0;
};

}  // namespace application
}  // namespace scanner
