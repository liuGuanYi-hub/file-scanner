#include "scanner/infrastructure/database_storage.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <sqlite3.h>

namespace scanner {
namespace infrastructure {

namespace {

// 简单 UUID v4 生成（不依赖外部库）
std::string generate_uuid_v4() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    uint64_t a = dis(gen);
    uint64_t b = dis(gen);

    oss << std::setw(8) << ((a >> 32) & 0xFFFFFFFF)
        << "-" << std::setw(4) << ((a >> 16) & 0xFFFF)
        << "-4" << std::setw(3) << (a & 0x0FFF)   // version 4
        << "-" << std::setw(4) << (((b >> 48) & 0x3FFF) | 0x8000) // variant
        << "-" << std::setw(12) << (b & 0xFFFFFFFFFFFF);

    return oss.str();
}

}  // namespace

DatabaseStorage::DatabaseStorage(const std::filesystem::path& db_path)
    : db_path_(db_path) {
    int rc = sqlite3_open(db_path_.u8string().c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("无法打开数据库: " + err);
    }

    // 启用 WAL 模式提升并发性能
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    create_tables();
}

DatabaseStorage::~DatabaseStorage() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void DatabaseStorage::create_tables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS scans (
            scan_id    TEXT PRIMARY KEY,
            root_path  TEXT NOT NULL,
            scan_time  TEXT NOT NULL,
            file_count INTEGER NOT NULL,
            total_bytes INTEGER NOT NULL,
            error_count INTEGER NOT NULL,
            elapsed_seconds REAL NOT NULL
        );

        CREATE TABLE IF NOT EXISTS files (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            scan_id   TEXT NOT NULL,
            path      TEXT NOT NULL,
            size      INTEGER NOT NULL,
            extension TEXT NOT NULL,
            FOREIGN KEY (scan_id) REFERENCES scans(scan_id) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_files_scan ON files(scan_id);
        CREATE INDEX IF NOT EXISTS idx_files_path ON files(path);
    )";

    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        throw std::runtime_error("创建表失败: " + msg);
    }
}

std::string DatabaseStorage::save_scan(const domain::ScanResult& result,
                                        const std::string& root_path) {
    auto scan_id = generate_uuid_v4();

    // 获取当前时间
    auto now = std::time(nullptr);
    std::ostringstream time_oss;
    time_oss << std::put_time(std::localtime(&now), "%Y-%m-%dT%H:%M:%S");

    // 插入扫描记录
    const char* insert_scan = R"(
        INSERT INTO scans (scan_id, root_path, scan_time, file_count,
                           total_bytes, error_count, elapsed_seconds)
        VALUES (?, ?, ?, ?, ?, ?, ?);
    )";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, insert_scan, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, scan_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, root_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, time_oss.str().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(result.file_count));
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(result.total_bytes));
    sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(result.error_count));
    sqlite3_bind_double(stmt, 7, result.elapsed_seconds);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw std::runtime_error("保存扫描记录失败: " + err);
    }
    sqlite3_finalize(stmt);

    // 批量插入文件元数据（使用事务）
    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* insert_file = R"(
        INSERT INTO files (scan_id, path, size, extension)
        VALUES (?, ?, ?, ?);
    )";

    sqlite3_prepare_v2(db_, insert_file, -1, &stmt, nullptr);
    for (const auto& file : result.files) {
        sqlite3_bind_text(stmt, 1, scan_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, file.path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(file.size));
        sqlite3_bind_text(stmt, 4, file.extension.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            throw std::runtime_error("保存文件记录失败");
        }
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);

    return scan_id;
}

std::vector<ScanRecord> DatabaseStorage::list_scans(int limit) {
    std::vector<ScanRecord> records;
    const char* sql = R"(
        SELECT scan_id, root_path, scan_time, file_count,
               total_bytes, error_count, elapsed_seconds
        FROM scans
        ORDER BY scan_time DESC
        LIMIT ?;
    )";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ScanRecord rec;
        rec.scan_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        rec.root_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        rec.scan_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        rec.file_count = static_cast<uint64_t>(sqlite3_column_int64(stmt, 3));
        rec.total_bytes = static_cast<uint64_t>(sqlite3_column_int64(stmt, 4));
        rec.error_count = static_cast<uint64_t>(sqlite3_column_int64(stmt, 5));
        rec.elapsed_seconds = sqlite3_column_double(stmt, 6);
        records.push_back(std::move(rec));
    }
    sqlite3_finalize(stmt);

    return records;
}

std::vector<domain::FileMetadata> DatabaseStorage::load_files(
    const std::string& scan_id) {
    std::vector<domain::FileMetadata> files;
    const char* sql = R"(
        SELECT path, size, extension FROM files WHERE scan_id = ?;
    )";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, scan_id.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        domain::FileMetadata meta;
        meta.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        meta.size = static_cast<std::uintmax_t>(sqlite3_column_int64(stmt, 2));
        meta.extension = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        files.push_back(std::move(meta));
    }
    sqlite3_finalize(stmt);

    return files;
}

std::vector<FileChange> DatabaseStorage::compare_scans(
    const std::string& scan_id_a, const std::string& scan_id_b) {
    std::vector<FileChange> changes;

    // 加载两次扫描的文件
    auto files_a = load_files(scan_id_a);
    auto files_b = load_files(scan_id_b);

    // 构建路径映射
    std::map<std::string, domain::FileMetadata> map_a, map_b;
    for (const auto& f : files_a) map_a[f.path] = f;
    for (const auto& f : files_b) map_b[f.path] = f;

    // 检测删除和修改
    for (const auto& [path, meta] : map_a) {
        auto it = map_b.find(path);
        if (it == map_b.end()) {
            changes.push_back({FileChange::REMOVED, path, meta.size, 0});
        } else if (it->second.size != meta.size) {
            changes.push_back({FileChange::MODIFIED, path, meta.size,
                               it->second.size});
        }
    }

    // 检测新增
    for (const auto& [path, meta] : map_b) {
        if (map_a.find(path) == map_a.end()) {
            changes.push_back({FileChange::ADDED, path, 0, meta.size});
        }
    }

    return changes;
}

void DatabaseStorage::delete_scan(const std::string& scan_id) {
    const char* sql = "DELETE FROM scans WHERE scan_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, scan_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

}  // namespace infrastructure
}  // namespace scanner
