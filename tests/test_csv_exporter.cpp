// CsvExporter 单元测试
#include "minitest.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "scanner/domain/types.h"
#include "scanner/infrastructure/csv_exporter.h"

namespace {

using scanner::infrastructure::CsvExporter;
using scanner::domain::FileMetadata;
using scanner::domain::ScanResult;

namespace fs = std::filesystem;

// 测试辅助：创建临时目录并在结束时清理
struct TestDir {
    explicit TestDir(const std::string& path) : path_(path) {
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~TestDir() { fs::remove_all(path_); }

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

std::string read_csv_content(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

// 创建测试用的临时目录
static TestDir tmp("tests/tmp_csv_csv");

// ============================================================
// 基础导出
// ============================================================
TEST(CsvExporter, EmptyResult) {
    CsvExporter exporter;
    ScanResult result;

    EXPECT_NO_THROW(exporter.export_result("tests/tmp_csv/empty.csv", result));

    // 验证文件存在且有 UTF-8 BOM + 表头
    std::string content = read_csv_content("tests/tmp_csv/empty.csv");
    EXPECT_TRUE(content.find("\xEF\xBB\xBF") == 0);
    EXPECT_NE(content.find("路径,大小（字节）,扩展名"), std::string::npos);
}

TEST(CsvExporter, NormalData) {
    CsvExporter exporter;
    ScanResult result;
    result.files.push_back({"src/main.cpp", 500u, ".cpp"});
    result.files.push_back({"include/test.h", 200u, ".h"});

    exporter.export_result("tests/tmp_csv/normal.csv", result);

    std::string content = read_csv_content("tests/tmp_csv/normal.csv");
    EXPECT_NE(content.find("src/main.cpp"), std::string::npos);
    EXPECT_NE(content.find("500"), std::string::npos);
    EXPECT_NE(content.find(".cpp"), std::string::npos);
    EXPECT_NE(content.find("include/test.h"), std::string::npos);
}

// ============================================================
// 特殊字符转义
// ============================================================
TEST(CsvExporter, CommaInPath) {
    CsvExporter exporter;
    ScanResult result;
    result.files.push_back({"a,b.cpp", 100u, ".cpp"});

    exporter.export_result("tests/tmp_csv/comma.csv", result);

    std::string content = read_csv_content("tests/tmp_csv/comma.csv");
    // 包含逗号的内容应被双引号包裹
    EXPECT_NE(content.find("\"a,b.cpp\""), std::string::npos);
}

TEST(CsvExporter, QuoteInPath) {
    CsvExporter exporter;
    ScanResult result;
    result.files.push_back({"file\"name.cpp", 100u, ".cpp"});

    exporter.export_result("tests/tmp_csv/quote.csv", result);

    std::string content = read_csv_content("tests/tmp_csv/quote.csv");
    // 引号应被转义为双引号
    EXPECT_NE(content.find("\"file\"\"name.cpp\""), std::string::npos);
}

TEST(CsvExporter, NewlineInPath) {
    CsvExporter exporter;
    ScanResult result;
    result.files.push_back({"line\nbreak.cpp", 100u, ".cpp"});

    exporter.export_result("tests/tmp_csv/newline.csv", result);

    std::string content = read_csv_content("tests/tmp_csv/newline.csv");
    // 换行符内容应被双引号包裹
    EXPECT_NE(content.find('"'), std::string::npos);
}

// ============================================================
// 中文路径
// ============================================================
TEST(CsvExporter, ChinesePath) {
    CsvExporter exporter;
    ScanResult result;
    result.files.push_back({"测试/文件.cpp", 500u, ".cpp"});

    EXPECT_NO_THROW(
        exporter.export_result("tests/tmp_csv/chinese.csv", result));

    std::string content = read_csv_content("tests/tmp_csv/chinese.csv");
    EXPECT_NE(content.find("测试/文件.cpp"), std::string::npos);
}

// ============================================================
// 错误处理
// ============================================================
TEST(CsvExporter, EmptyPathThrows) {
    CsvExporter exporter;
    ScanResult result;
    EXPECT_THROW(
        { exporter.export_result("", result); },
        std::invalid_argument);
}

TEST(CsvExporter, AutoCreatesDirectory) {
    CsvExporter exporter;
    ScanResult result;
    result.files.push_back({"test.cpp", 100u, ".cpp"});

    EXPECT_NO_THROW(
        exporter.export_result("tests/tmp_csv/subdir/result.csv", result));
    EXPECT_TRUE(fs::exists("tests/tmp_csv/subdir/result.csv"));
}

// ============================================================
// 数据完整性
// ============================================================
TEST(CsvExporter, RowCountMatchesData) {
    CsvExporter exporter;
    ScanResult result;
    for (int i = 0; i < 100; ++i) {
        result.files.push_back(
            {"file_" + std::to_string(i) + ".cpp", 100u, ".cpp"});
    }

    exporter.export_result("tests/tmp_csv/count.csv", result);

    std::string content = read_csv_content("tests/tmp_csv/count.csv");
    // 表头 + 100 行数据
    std::size_t newlines = 0;
    for (char c : content) {
        if (c == '\n') ++newlines;
    }
    EXPECT_EQ(newlines, 101u);  // header + 100 data rows with trailing newline
}

}  // namespace
