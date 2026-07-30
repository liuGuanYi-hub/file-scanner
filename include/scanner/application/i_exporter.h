#pragma once

#include <filesystem>

#include "scanner/domain/types.h"

namespace scanner {
namespace application {

// === 导出器接口 ===
// 所有结果导出器（CSV, JSON 等）都必须实现此接口
class IExporter {
public:
    virtual ~IExporter() = default;

    // 将扫描结果导出到指定路径
    virtual void export_result(const std::filesystem::path& output_path,
                               const domain::ScanResult& result) = 0;
};

}  // namespace application
}  // namespace scanner
