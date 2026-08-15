#pragma once

#include <cstdint>
#include <memory>

#include "scanner/application/i_scan_tool.h"

namespace scanner {
namespace infrastructure {

// 创建"目录扫描"工具（包装 FileScanner）
// 新增工具示例：只需返回一个实现 IScanTool 的对象并在注册表注册
std::shared_ptr<application::IScanTool> make_scan_tool();

// 创建"重复文件检测"工具（包装 DuplicateFinder）
std::shared_ptr<application::IScanTool> make_duplicates_tool();

// 创建"大文件扫描"工具（全新工具示例，不依赖已有基础设施实现）
std::shared_ptr<application::IScanTool> make_big_files_tool(std::uintmax_t min_size);

}  // namespace infrastructure
}  // namespace scanner
