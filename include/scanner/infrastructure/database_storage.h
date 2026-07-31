#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "scanner/domain/types.h"

// 前向声明 sqlite3（避免在头文件中引入 sqlite3.h）
typedef struct sqlite3 sqlite3;

namespace scanner {
namespace infrastructure {

// === 扫描记录 ===
struct ScanRecord {
    std::string scan_id;          // UUID
    std::string root_path;
    std::string scan_time;        // ISO 8601
    std::uint64_t file_count = 0;
    std::uint64_t total_bytes = 0;
    std::uint64_t error_count = 0;
    double elapsed_seconds = 0.0;
};

// === 文件变更记录 ===
struct FileChange {
    enum Type { ADDED, REMOVED, MODIFIED };
    Type type;
    std::string path;
    std::uintmax_t old_size = 0;
    std::uintmax_t new_size = 0;
};

// === 数据库持久化存储 ===
class DatabaseStorage {
public:
    explicit DatabaseStorage(const std::filesystem::path& db_path);
    ~DatabaseStorage();

    DatabaseStorage(const DatabaseStorage&) = delete;
    DatabaseStorage& operator=(const DatabaseStorage&) = delete;

    // 保存扫描结果
    std::string save_scan(const domain::ScanResult& result,
                          const std::string& root_path);

    // 加载最近的扫描记录
    std::vector<ScanRecord> list_scans(int limit = 10);

    // 加载指定扫描的文件元数据
    std::vector<domain::FileMetadata> load_files(const std::string& scan_id);

    // 比较两次扫描的差异
    std::vector<FileChange> compare_scans(const std::string& scan_id_a,
                                          const std::string& scan_id_b);

    // 删除指定扫描记录
    void delete_scan(const std::string& scan_id);

private:
    void create_tables();

    std::filesystem::path db_path_;
    sqlite3* db_ = nullptr;
};

}  // namespace infrastructure
}  // namespace scanner
