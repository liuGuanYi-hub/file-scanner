#include "scanner/application/tool_registry.h"

namespace scanner {
namespace application {

void ToolRegistry::register_tool(std::shared_ptr<IScanTool> tool) {
    if (tool) tools_[tool->name()] = std::move(tool);
}

std::shared_ptr<IScanTool> ToolRegistry::get(const std::string& name) const {
    auto it = tools_.find(name);
    return it == tools_.end() ? nullptr : it->second;
}

bool ToolRegistry::contains(const std::string& name) const {
    return tools_.count(name) > 0;
}

std::vector<std::string> ToolRegistry::names() const {
    std::vector<std::string> result;
    result.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        result.push_back(name);
    }
    return result;
}

std::size_t ToolRegistry::size() const { return tools_.size(); }

}  // namespace application
}  // namespace scanner
