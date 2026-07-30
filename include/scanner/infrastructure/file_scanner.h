#pragma once

#include "scanner/application/i_logger.h"
#include "scanner/application/i_scanner.h"

namespace scanner {
namespace infrastructure {

// === 文件扫描器实现 ===
// 实现 IScanner 接口，封装递归目录遍历和多线程文件处理逻辑
// 通过构造函数注入 ILogger（可选），实现依赖反转
class FileScanner : public application::IScanner {
public:
    // logger 可选 — 传 nullptr 则不记录错误日志
    explicit FileScanner(application::ILogger* logger = nullptr);

    // 执行一次完整的目录扫描
    domain::ScanResult scan(const domain::ScanOptions& options) override;

private:
    application::ILogger* logger_;
};

}  // namespace infrastructure
}  // namespace scanner
