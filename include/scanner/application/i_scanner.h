#pragma once

#include "scanner/domain/types.h"

namespace scanner {
namespace application {

// === 扫描器接口 ===
// 所有扫描器实现都必须实现此接口
class IScanner {
public:
    virtual ~IScanner() = default;

    // 执行一次完整的目录扫描
    // 参数: options - 扫描配置（目标目录、线程数、过滤器等）
    // 返回: ScanResult - 扫描统计结果和文件元数据
    virtual domain::ScanResult scan(const domain::ScanOptions& options) = 0;
};

}  // namespace application
}  // namespace scanner
