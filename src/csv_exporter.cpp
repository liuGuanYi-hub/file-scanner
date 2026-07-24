#include "csv_exporter.h"

#include <fstream>
#include <stdexcept>
#include <string>

namespace scanner {

namespace {

std::string escape_csv_field(const std::string& value) {
    bool needs_quotes = false;
    for (const char character : value) {
        if (character == ',' || character == '"' || character == '\r' ||
            character == '\n') {
            needs_quotes = true;
            break;
        }
    }

    if (!needs_quotes) {
        return value;
    }

    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char character : value) {
        escaped.push_back(character);
        if (character == '"') {
            escaped.push_back('"');
        }
    }
    escaped.push_back('"');
    return escaped;
}

}  // namespace

void export_scan_result_csv(const std::filesystem::path& output_path,
                            const ScanResult& result) {
    if (output_path.empty()) {
        throw std::invalid_argument("CSV 输出路径不能为空");
    }

    std::error_code directory_error;
    const std::filesystem::path parent = output_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, directory_error);
        if (directory_error) {
            throw std::runtime_error("无法创建 CSV 输出目录: " +
                                     directory_error.message());
        }
    }

    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("无法打开 CSV 输出文件: " + output_path.u8string());
    }

    // UTF-8 BOM 方便 Excel 在 Windows 下正确识别中文表头和路径。
    output.write("\xEF\xBB\xBF", 3);
    output << "路径,大小（字节）,扩展名\n";
    for (const FileMetadata& file : result.files) {
        output << escape_csv_field(file.path) << ',' << file.size << ','
               << escape_csv_field(file.extension) << '\n';
    }

    output.flush();
    if (!output) {
        throw std::runtime_error("写入 CSV 输出文件失败: " + output_path.u8string());
    }
}

}  // namespace scanner
