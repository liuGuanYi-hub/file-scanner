#pragma once

#include <cstdint>
#include <string>

namespace scanner {
namespace application {

// === 日志接口 ===
// 所有日志记录器都必须实现此接口
class ILogger {
public:
    virtual ~ILogger() = default;

    // 记录一条错误日志
    // 参数: path - 发生错误的文件路径
    //       message - 错误描述
    // 注意: 实现必须是线程安全的
    virtual void log(const std::string& path,
                     const std::string& message) noexcept = 0;

    // 返回日志写入失败的次数
    virtual std::uint64_t failure_count() const noexcept = 0;
};

}  // namespace application
}  // namespace scanner
