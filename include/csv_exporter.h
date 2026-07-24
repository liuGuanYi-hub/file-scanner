#pragma once

#include <filesystem>

#include "scan_engine.h"

namespace scanner {

void export_scan_result_csv(const std::filesystem::path& output_path,
                            const ScanResult& result);

}  // namespace scanner
