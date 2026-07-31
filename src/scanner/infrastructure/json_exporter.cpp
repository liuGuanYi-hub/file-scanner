#include "scanner/infrastructure/json_exporter.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace scanner {
namespace infrastructure {

namespace {

std::string escape_json(const std::string& value) {
    std::ostringstream oss;
    for (char c : value) {
        switch (c) {
            case '"':  oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': oss << "\\n";  break;
            case '\r': oss << "\\r";  break;
            case '\t': oss << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    oss << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(c));
                } else {
                    oss << c;
                }
        }
    }
    return oss.str();
}

}  // namespace

void JsonExporter::export_result(const std::filesystem::path& output_path,
                                  const domain::ScanResult& result) {
    if (output_path.empty()) {
        throw std::invalid_argument("JSON 输出路径不能为空");
    }

    std::error_code ec;
    auto parent = output_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            throw std::runtime_error("无法创建输出目录: " + ec.message());
        }
    }

    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("无法打开 JSON 文件: " + output_path.u8string());
    }

    out << "{\n";
    out << "  \"file_count\": " << result.file_count << ",\n";
    out << "  \"total_bytes\": " << result.total_bytes << ",\n";
    out << "  \"error_count\": " << result.error_count << ",\n";
    out << "  \"elapsed_seconds\": " << std::fixed << std::setprecision(3)
        << result.elapsed_seconds << ",\n";
    out << "  \"cancelled\": " << (result.cancelled ? "true" : "false") << ",\n";

    // 扩展名统计
    out << "  \"extensions\": {\n";
    bool first_ext = true;
    for (const auto& [ext, count] : result.extension_counts) {
        if (!first_ext) out << ",\n";
        out << "    \"" << escape_json(ext) << "\": " << count;
        first_ext = false;
    }
    out << "\n  },\n";

    // 文件列表
    out << "  \"files\": [\n";
    for (size_t i = 0; i < result.files.size(); ++i) {
        if (i > 0) out << ",\n";
        const auto& f = result.files[i];
        out << "    {\"path\": \"" << escape_json(f.path) << "\""
            << ", \"size\": " << f.size
            << ", \"ext\": \"" << escape_json(f.extension) << "\"}";
    }
    out << "\n  ],\n";

    // 错误列表
    out << "  \"errors\": [\n";
    for (size_t i = 0; i < result.errors.size(); ++i) {
        if (i > 0) out << ",\n";
        out << "    {\"path\": \"" << escape_json(result.errors[i].path)
            << "\", \"message\": \"" << escape_json(result.errors[i].message)
            << "\"}";
    }
    out << "\n  ]\n";
    out << "}\n";

    out.flush();
    if (!out) {
        throw std::runtime_error("写入 JSON 文件失败");
    }
}

}  // namespace infrastructure
}  // namespace scanner
