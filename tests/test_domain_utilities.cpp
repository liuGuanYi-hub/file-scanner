// domain::utilities 单元测试
#include "minitest.h"
#include <filesystem>
#include <set>
#include <string>

#include "scanner/domain/utilities.h"

namespace scanner {
namespace domain {
namespace {

namespace fs = std::filesystem;

// ============================================================
// normalize_extension
// ============================================================
TEST(NormalizeExtension, LowercaseConversion) {
    EXPECT_EQ(normalize_extension(".CPP"), ".cpp");
    EXPECT_EQ(normalize_extension(".H"), ".h");
    EXPECT_EQ(normalize_extension(".Txt"), ".txt");
}

TEST(NormalizeExtension, AddsDotPrefix) {
    EXPECT_EQ(normalize_extension("cpp"), ".cpp");
    EXPECT_EQ(normalize_extension("h"), ".h");
}

TEST(NormalizeExtension, EmptyAndStarReturnEmpty) {
    EXPECT_EQ(normalize_extension(""), "");
    EXPECT_EQ(normalize_extension("*"), "");
}

TEST(NormalizeExtension, NoneBecomesNoExtension) {
    EXPECT_EQ(normalize_extension("none"), "[无扩展名]");
    EXPECT_EQ(normalize_extension("[无扩展名]"), "[无扩展名]");
}

TEST(NormalizeExtension, TrimsWhitespace) {
    EXPECT_EQ(normalize_extension("  .cpp  "), ".cpp");
}

// ============================================================
// extension_for_path
// ============================================================
TEST(ExtensionForPath, RegularFile) {
    EXPECT_EQ(extension_for_path(fs::path("test.cpp")), ".cpp");
    EXPECT_EQ(extension_for_path(fs::path("test.h")), ".h");
}

TEST(ExtensionForPath, NoExtension) {
    EXPECT_EQ(extension_for_path(fs::path("Makefile")), "[无扩展名]");
    EXPECT_EQ(extension_for_path(fs::path("README")), "[无扩展名]");
}

TEST(ExtensionForPath, MultipleDots) {
    EXPECT_EQ(extension_for_path(fs::path("archive.tar.gz")), ".gz");
}

// ============================================================
// parse_extensions
// ============================================================
TEST(ParseExtensions, CommaSeparated) {
    auto result = parse_extensions(std::string("cpp,h,txt"));
    EXPECT_EQ(result.size(), 3u);
    EXPECT_NE(result.find(".cpp"), result.end());
    EXPECT_NE(result.find(".h"), result.end());
    EXPECT_NE(result.find(".txt"), result.end());
}

TEST(ParseExtensions, DottedAndUndotted) {
    auto result = parse_extensions(std::string(".cpp,h,.txt"));
    EXPECT_EQ(result.size(), 3u);
}

TEST(ParseExtensions, Deduplicates) {
    auto result = parse_extensions(std::string("cpp,cpp,CPP"));
    EXPECT_EQ(result.size(), 1u);
}

TEST(ParseExtensions, EmptyInput) {
    auto result = parse_extensions(std::string(""));
    EXPECT_TRUE(result.empty());
}

TEST(ParseExtensions, NoneKeyword) {
    auto result = parse_extensions(std::string("none"));
    EXPECT_NE(result.find("[无扩展名]"), result.end());
}

// ============================================================
// parse_size_bytes
// ============================================================
TEST(ParseSizeBytes, ValidNumber) {
    auto result = parse_size_bytes(std::string("1024"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1024u);
}

TEST(ParseSizeBytes, Zero) {
    auto result = parse_size_bytes(std::string("0"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0u);
}

TEST(ParseSizeBytes, EmptyThrows) {
    EXPECT_THROW(parse_size_bytes(std::string("")), std::invalid_argument);
}

TEST(ParseSizeBytes, NonNumericThrows) {
    EXPECT_THROW(parse_size_bytes(std::string("abc")), std::invalid_argument);
}

// ============================================================
// parse_worker_count
// ============================================================
TEST(ParseWorkerCount, ValidRange) {
    EXPECT_EQ(parse_worker_count(std::string("1")), 1u);
    EXPECT_EQ(parse_worker_count(std::string("4")), 4u);
    EXPECT_EQ(parse_worker_count(std::string("64")), 64u);
}

TEST(ParseWorkerCount, ZeroThrows) {
    EXPECT_THROW(parse_worker_count(std::string("0")), std::exception);
}

TEST(ParseWorkerCount, Over64Throws) {
    EXPECT_THROW(parse_worker_count(std::string("65")), std::exception);
    EXPECT_THROW(parse_worker_count(std::string("999")), std::exception);
}

// ============================================================
// default_worker_count
// ============================================================
TEST(DefaultWorkerCount, ReturnsPositive) {
    auto count = default_worker_count();
    EXPECT_GT(count, 0u);
    EXPECT_LE(count, 8u);
}

// ============================================================
// display_path
// ============================================================
TEST(DisplayPath, ReturnsNonEmpty) {
    auto result = display_path(fs::path("."));
    EXPECT_FALSE(result.empty());
}

// ============================================================
// trim_ascii
// ============================================================
TEST(TrimAscii, TrimsWhitespace) {
    EXPECT_EQ(trim_ascii("  hello  "), "hello");
    EXPECT_EQ(trim_ascii("\t\n text \r\n"), "text");
}

TEST(TrimAscii, NoWhitespace) {
    EXPECT_EQ(trim_ascii("hello"), "hello");
}

}  // namespace
}  // namespace domain
}  // namespace scanner
