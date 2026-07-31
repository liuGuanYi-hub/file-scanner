#pragma once

#include <filesystem>

#include "scanner/application/i_exporter.h"

namespace scanner {
namespace infrastructure {

// === JSON 导出器 ===
class JsonExporter : public application::IExporter {
public:
    void export_result(const std::filesystem::path& output_path,
                       const domain::ScanResult& result) override;
};

}  // namespace infrastructure
}  // namespace scanner
