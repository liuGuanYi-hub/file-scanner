#include "scanner/infrastructure/duplicate_finder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <openssl/evp.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <memory>

#include <sqlite3.h>

namespace scanner {
namespace infrastructure {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kPartialReadSize = 64 * 1024;  // 64KB

// 预哈希缓存结构版本：算法或表结构变化时递增，使旧缓存整体失效
constexpr int kPrehashCacheVersion = 1;

// === 预哈希缓存（对标 czkawka prehash cache） ===
// 把 64KB 预哈希中间结果存入 SQLite，二次扫描命中时跳过文件 I/O。
// 用 (path, size, mtime) 校验缓存是否仍然有效。
class PrehashCache {
public:
    explicit PrehashCache(const std::filesystem::path& db_path)
        : db_(nullptr) {
        if (sqlite3_open(db_path.u8string().c_str(), &db_) != SQLITE_OK) {
            db_ = nullptr;
            return;
        }
        exec("CREATE TABLE IF NOT EXISTS prehash_cache ("
             "path TEXT PRIMARY KEY,"
             "size INTEGER NOT NULL,"
             "mtime INTEGER NOT NULL,"
             "prehash TEXT NOT NULL,"
             "ver INTEGER NOT NULL)");
        // 版本不一致时清空旧缓存
        exec("DELETE FROM prehash_cache WHERE ver != " +
             std::to_string(kPrehashCacheVersion));
    }

    ~PrehashCache() {
        if (db_) sqlite3_close(db_);
    }

    bool valid() const { return db_ != nullptr; }

    // 读取缓存：命中且 size/mtime 匹配返回 true，prehash 有效
    bool get(const std::string& path, std::uintmax_t size,
             std::uintmax_t mtime, std::string& prehash) {
        if (!db_) return false;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT prehash FROM prehash_cache "
                          "WHERE path=?1 AND size=?2 AND mtime=?3";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(size));
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(mtime));
        bool ok = (sqlite3_step(stmt) == SQLITE_ROW);
        if (ok) {
            const unsigned char* text = sqlite3_column_text(stmt, 0);
            if (text) prehash.assign(reinterpret_cast<const char*>(text));
        }
        sqlite3_finalize(stmt);
        return ok;
    }

    // 写入缓存（UPSERT）
    void put(const std::string& path, std::uintmax_t size,
             std::uintmax_t mtime, const std::string& prehash) {
        if (!db_) return;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT OR REPLACE INTO prehash_cache "
                          "(path, size, mtime, prehash, ver) "
                          "VALUES (?1, ?2, ?3, ?4, ?5)";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return;
        sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(size));
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(mtime));
        sqlite3_bind_text(stmt, 4, prehash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, kPrehashCacheVersion);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    void begin_transaction() { exec("BEGIN TRANSACTION"); }
    void commit() { exec("COMMIT"); }

private:
    void exec(const std::string& sql) {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) !=
            SQLITE_OK) {
            sqlite3_free(err);
        }
    }

    sqlite3* db_;
};

// 获取文件修改时间（epoch 秒）
std::uintmax_t file_mtime(const std::filesystem::path& p) {
    std::error_code ec;
    auto ft = std::filesystem::last_write_time(p, ec);
    if (ec) return 0;
    return static_cast<std::uintmax_t>(
        ft.time_since_epoch().count());
}

// 字节数组转十六进制字符串
std::string to_hex(const unsigned char* data, std::size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<unsigned int>(data[i]);
    }
    return oss.str();
}

// 从流中读取一段数据并计算 SHA256
std::string compute_sha256(std::ifstream& stream, std::size_t max_bytes) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return {};

    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);

    std::array<char, 8192> buffer{};
    std::size_t total_read = 0;

    while (total_read < max_bytes) {
        std::size_t to_read = std::min(buffer.size(), max_bytes - total_read);
        stream.read(buffer.data(), static_cast<std::streamsize>(to_read));
        std::streamsize bytes_read = stream.gcount();
        if (bytes_read <= 0) break;
        EVP_DigestUpdate(ctx, buffer.data(),
                         static_cast<std::size_t>(bytes_read));
        total_read += static_cast<std::size_t>(bytes_read);
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);

    return to_hex(hash, hash_len);
}

}  // namespace

std::string DuplicateFinder::hash_file(const fs::path& path, bool partial) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }

    if (partial) {
        return compute_sha256(file, kPartialReadSize);
    } else {
        return compute_sha256(file, std::numeric_limits<std::size_t>::max());
    }
}

std::vector<DuplicateGroup> DuplicateFinder::find(
    const fs::path& root, std::size_t worker_count, DuplicateMode mode,
    const fs::path* cache_db) {
    if (worker_count == 0 || worker_count > 64) {
        throw std::invalid_argument("线程数必须是 1 到 64 之间的整数");
    }

    // 预哈希缓存（可选）
    std::unique_ptr<PrehashCache> cache_ptr;
    if (cache_db) {
        cache_ptr = std::make_unique<PrehashCache>(*cache_db);
    }
    const bool use_cache = cache_ptr && cache_ptr->valid();

    // ============================================================
    // 第 1 步：遍历目录，按 大小（Content）或 文件名+大小（NameAndSize）分组
    // ============================================================
    // Content:    size -> {path, path, ...}
    // NameAndSize: (filename, size) -> {path, path, ...}  （零 I/O 预筛）
    std::map<std::pair<std::string, std::uintmax_t>, std::vector<fs::path>> name_size_groups;
    std::map<std::uintmax_t, std::vector<fs::path>> size_groups;

    std::error_code ec;
    for (fs::recursive_directory_iterator it(
             root, fs::directory_options::skip_permission_denied, ec),
         end;
         it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code entry_ec;
        if (it->is_regular_file(entry_ec)) {
            std::error_code size_ec;
            auto size = fs::file_size(it->path(), size_ec);
            if (size_ec) continue;
            if (mode == DuplicateMode::NameAndSize) {
                std::string name = it->path().filename().u8string();
                name_size_groups[{name, size}].push_back(it->path());
            } else {
                size_groups[size].push_back(it->path());
            }
        }
    }

    // 统一转成候选组集合：每组内文件数 >= 2 才需要进一步哈希
    std::vector<std::vector<fs::path>> candidate_groups;
    if (mode == DuplicateMode::NameAndSize) {
        for (auto& [key, paths] : name_size_groups) {
            if (paths.size() >= 2) candidate_groups.push_back(std::move(paths));
        }
    } else {
        for (auto& [size, paths] : size_groups) {
            if (paths.size() >= 2) candidate_groups.push_back(std::move(paths));
        }
    }

    // ============================================================
    // 第 2 步：对候选组计算前 64KB 哈希
    //          （启用缓存时：命中跳过 I/O，未命中计算后写回）
    // ============================================================
    // partial_hash -> {path, path, ...}
    std::map<std::string, std::vector<fs::path>> partial_hash_groups;

    if (use_cache) cache_ptr->begin_transaction();
    for (auto& paths : candidate_groups) {
        for (const auto& p : paths) {
            std::string hash;
            if (use_cache) {
                auto size = fs::file_size(p);
                auto mtime = file_mtime(p);
                std::string cached;
                if (cache_ptr->get(p.u8string(), size, mtime, cached)) {
                    hash = cached;
                } else {
                    hash = hash_file(p, true);
                    cache_ptr->put(p.u8string(), size, mtime, hash);
                }
            } else {
                hash = hash_file(p, true);
            }
            if (!hash.empty()) {
                partial_hash_groups[hash].push_back(p);
            }
        }
    }
    if (use_cache) cache_ptr->commit();

    // ============================================================
    // 第 3 步：对同哈希的文件组计算完整文件哈希
    // ============================================================
    std::vector<DuplicateGroup> results;

    for (auto& [partial_hash, paths] : partial_hash_groups) {
        if (paths.size() < 2) continue;

        // 完整文件哈希 -> 路径
        std::map<std::string, std::vector<fs::path>> full_hash_groups;
        for (const auto& p : paths) {
            auto hash = hash_file(p, false);
            if (!hash.empty()) {
                full_hash_groups[hash].push_back(p);
            }
        }

        for (auto& [full_hash, group_paths] : full_hash_groups) {
            if (group_paths.size() < 2) continue;

            DuplicateGroup group;
            group.hash = full_hash;
            group.file_size = fs::file_size(group_paths[0]);

            for (const auto& p : group_paths) {
                group.paths.push_back(p.u8string());
            }
            results.push_back(std::move(group));
        }
    }

    return results;
}

}  // namespace infrastructure
}  // namespace scanner
