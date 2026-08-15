#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "scanner/application/i_scan_tool.h"

namespace scanner {
namespace application {

// === 工具注册表（对标 czkawka 多工具插件化组织） ===
// 所有扫描/清理工具按名称注册，CLI/GUI 通过 get() 按名调度，
// 新增工具零改动调用方代码。
class ToolRegistry {
public:
    // 注册一个工具（同名覆盖）
    void register_tool(std::shared_ptr<IScanTool> tool);

    // 按名称获取工具；不存在返回 nullptr
    std::shared_ptr<IScanTool> get(const std::string& name) const;

    // 是否已注册
    bool contains(const std::string& name) const;

    // 全部已注册工具名（按字典序）
    std::vector<std::string> names() const;

    // 已注册数量
    std::size_t size() const;

private:
    std::map<std::string, std::shared_ptr<IScanTool>> tools_;
};

}  // namespace application
}  // namespace scanner
