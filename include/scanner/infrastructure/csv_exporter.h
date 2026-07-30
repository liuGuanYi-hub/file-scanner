#pragma once

#include <filesystem>

#include "scanner/application/i_exporter.h"

namespace scanner {
namespace infrastructure {

// === CSV 导出器 ===
// 实现 IExporter 接口，将扫描结果导出为 UTF-8 CSV 文件
class CsvExporter : public application::IExporter {
public:
    void export_result(const std::filesystem::path& output_path,
                       const domain::ScanResult& result) override;
};

}  // namespace infrastructure
}  // namespace scanner
