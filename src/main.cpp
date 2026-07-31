#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "scanner/domain/types.h"
#include "scanner/domain/utilities.h"
#include "scanner/infrastructure/csv_exporter.h"
#include "scanner/infrastructure/database_storage.h"
#include "scanner/infrastructure/duplicate_finder.h"
#include "scanner/infrastructure/error_logger.h"
#include "scanner/infrastructure/file_scanner.h"
#include "scanner/infrastructure/json_exporter.h"

namespace fs = std::filesystem;
using namespace scanner::domain;

namespace {

struct CommandLineOptions {
    fs::path root = fs::current_path();
    std::size_t worker_count = scanner::domain::default_worker_count();
    FileFilter filter;
    std::optional<fs::path> log_path;
    std::optional<fs::path> output_path;
    std::optional<fs::path> db_path;
    std::optional<fs::path> json_path;
    bool show_help = false;
    bool show_dupes = false;
    bool list_scans = false;
    std::optional<std::string> compare_id;
};

// === 前面的模板函数保持不变 ===
template <typename Char>
std::basic_string<Char> make_text(const char* text) {
    std::basic_string<Char> result;
    while (*text != '\0') {
        result.push_back(static_cast<Char>(*text));
        ++text;
    }
    return result;
}

template <typename Char>
std::set<std::string> parse_extensions_argument(
    const std::basic_string<Char>& value) {
    if constexpr (std::is_same_v<Char, wchar_t>) {
        return scanner::domain::parse_extensions(std::wstring(value));
    } else {
        return scanner::domain::parse_extensions(std::string(value));
    }
}

template <typename Char>
std::optional<std::uintmax_t> parse_size_argument(
    const std::basic_string<Char>& value) {
    if constexpr (std::is_same_v<Char, wchar_t>) {
        return scanner::domain::parse_size_bytes(std::wstring(value));
    } else {
        return scanner::domain::parse_size_bytes(std::string(value));
    }
}

template <typename Char, typename Callback>
bool consume_option_value(int& index,
                          int argc,
                          Char* argv[],
                          const std::basic_string<Char>& argument,
                          const char* name,
                          Callback callback) {
    const std::basic_string<Char> option = make_text<Char>(name);
    if (argument == option) {
        if (index + 1 >= argc) {
            throw std::invalid_argument(std::string(name) + " 缺少参数值");
        }
        callback(std::basic_string<Char>(argv[++index]));
        return true;
    }
    const std::basic_string<Char> prefix = option + make_text<Char>("=");
    if (argument.rfind(prefix, 0) == 0) {
        callback(argument.substr(prefix.size()));
        return true;
    }
    return false;
}

template <typename Char, typename Callback>
bool consume_option_aliases(int& index,
                            int argc,
                            Char* argv[],
                            const std::basic_string<Char>& argument,
                            std::initializer_list<const char*> names,
                            Callback callback) {
    for (const char* name : names) {
        if (consume_option_value(index, argc, argv, argument, name, callback)) {
            return true;
        }
    }
    return false;
}

template <typename Char>
bool consume_flag(const std::basic_string<Char>& argument,
                  const char* name) {
    return argument == make_text<Char>(name);
}

template <typename Char>
void require_option_value(const std::basic_string<Char>& value,
                          const char* name) {
    if (value.empty()) {
        throw std::invalid_argument(std::string(name) + " 的参数不能为空");
    }
}

template <typename Char>
CommandLineOptions parse_arguments(int argc, Char* argv[]) {
    CommandLineOptions options;
    bool root_set = false;
    bool worker_set = false;

    for (int index = 1; index < argc; ++index) {
        const std::basic_string<Char> argument(argv[index]);
        if (argument == make_text<Char>("--help") ||
            argument == make_text<Char>("-h")) {
            options.show_help = true;
            continue;
        }
        if (consume_flag(argument, "--dupes")) {
            options.show_dupes = true; continue;
        }
        if (consume_flag(argument, "--list-scans")) {
            options.list_scans = true; continue;
        }

        if (consume_option_aliases(index, argc, argv, argument,
                                   {"--path", "-p"},
                                   [&](const auto& v) {
                                       require_option_value(v, "--path");
                                       options.root = fs::path(v);
                                       root_set = true;
                                   })) continue;
        if (consume_option_aliases(index, argc, argv, argument,
                                   {"--threads", "-t"},
                                   [&](const auto& v) {
                                       if constexpr (std::is_same_v<Char, wchar_t>)
                                           options.worker_count = scanner::domain::parse_worker_count(std::wstring(v));
                                       else
                                           options.worker_count = scanner::domain::parse_worker_count(std::string(v));
                                       worker_set = true;
                                   })) continue;
        if (consume_option_aliases(index, argc, argv, argument,
                                   {"--ext", "-e"},
                                   [&](const auto& v) {
                                       require_option_value(v, "--ext");
                                       options.filter.extensions = parse_extensions_argument(v);
                                   })) continue;
        if (consume_option_aliases(index, argc, argv, argument,
                                   {"--min-size", "-m"},
                                   [&](const auto& v) {
                                       options.filter.min_size = parse_size_argument(v);
                                   })) continue;
        if (consume_option_aliases(index, argc, argv, argument,
                                   {"--max-size", "-M"},
                                   [&](const auto& v) {
                                       options.filter.max_size = parse_size_argument(v);
                                   })) continue;
        if (consume_option_aliases(index, argc, argv, argument,
                                   {"--log", "-l"},
                                   [&](const auto& v) {
                                       require_option_value(v, "--log");
                                       options.log_path = fs::path(v);
                                   })) continue;
        if (consume_option_aliases(index, argc, argv, argument,
                                   {"--output", "-o"},
                                   [&](const auto& v) {
                                       require_option_value(v, "--output");
                                       options.output_path = fs::path(v);
                                   })) continue;
        if (consume_option_aliases(index, argc, argv, argument,
                                   {"--db"},
                                   [&](const auto& v) {
                                       require_option_value(v, "--db");
                                       options.db_path = fs::path(v);
                                   })) continue;
        if (consume_option_aliases(index, argc, argv, argument,
                                   {"--json"},
                                   [&](const auto& v) {
                                       require_option_value(v, "--json");
                                       options.json_path = fs::path(v);
                                   })) continue;
        if (consume_option_aliases(index, argc, argv, argument,
                                   {"--compare"},
                                   [&](const auto& v) {
                                       std::string s;
                                       if constexpr (std::is_same_v<Char, wchar_t>) {
                                           std::wstring ws(v);
                                           s.assign(ws.begin(), ws.end());
                                       } else { s = std::string(v); }
                                       options.compare_id = s;
                                   })) continue;

        if (!argument.empty() && argument.front() == static_cast<Char>('-')) {
            throw std::invalid_argument("未知选项，请使用 --help 查看用法");
        }
        if (!root_set) {
            options.root = fs::path(argument); root_set = true;
        } else if (!worker_set) {
            if constexpr (std::is_same_v<Char, wchar_t>)
                options.worker_count = scanner::domain::parse_worker_count(std::wstring(argument));
            else
                options.worker_count = scanner::domain::parse_worker_count(std::string(argument));
            worker_set = true;
        } else {
            throw std::invalid_argument("位置参数过多");
        }
    }

    if (options.filter.min_size && options.filter.max_size &&
        *options.filter.min_size > *options.filter.max_size) {
        throw std::invalid_argument("最小文件大小不能大于最大文件大小");
    }
    return options;
}

void print_usage() {
    std::cout
        << "C++ 多线程文件扫描器 v2.0\n\n"
        << "用法: file_scanner [目录] [线程数] [选项]\n\n"
        << "扫描选项:\n"
        << "  --path, -p <目录>     扫描目录 (默认当前)\n"
        << "  --threads, -t <数>    线程数 1-64 (默认自动)\n"
        << "  --ext, -e <列表>      扩展名过滤 .cpp,.h\n"
        << "  --min-size, -m <字节> 最小文件大小\n"
        << "  --max-size, -M <字节> 最大文件大小\n"
        << "  --log, -l <文件>      错误日志\n"
        << "  --output, -o <文件>   CSV 输出\n\n"
        << "v2.0 新增:\n"
        << "  --json <文件>         JSON 格式输出\n"
        << "  --dupes               检测重复文件\n"
        << "  --db <文件>           SQLite 数据库保存\n"
        << "  --list-scans          列出历史扫描记录\n"
        << "  --compare <id>        比较当前与历史扫描\n"
        << "  --help, -h            帮助\n";
}

void print_filter(const FileFilter& filter) {
    if (filter.extensions.empty() && !filter.min_size && !filter.max_size) return;
    std::cout << "过滤条件:\n";
    if (!filter.extensions.empty()) {
        std::cout << "  扩展名: ";
        bool first = true;
        for (const auto& ext : filter.extensions) {
            if (!first) std::cout << ",";
            std::cout << ext;
            first = false;
        }
        std::cout << "\n";
    }
    if (filter.min_size) std::cout << "  最小大小: " << *filter.min_size << " 字节\n";
    if (filter.max_size) std::cout << "  最大大小: " << *filter.max_size << " 字节\n";
}

void print_scan_result(const ScanResult& result,
                       const fs::path& root,
                       std::size_t worker_count) {
    std::cout << "\n扫描完成\n"
              << "目录: " << scanner::domain::display_path(root) << "\n"
              << "线程数: " << worker_count << "\n"
              << "文件数: " << result.file_count << "\n"
              << "总大小: " << std::fixed << std::setprecision(2)
              << static_cast<double>(result.total_bytes) / (1024.0 * 1024.0) << " MiB\n"
              << "错误数: " << result.error_count << "\n"
              << "元数据记录: " << result.files.size() << " 条\n"
              << "耗时: " << result.elapsed_seconds << " 秒\n\n";

    if (!result.errors.empty()) {
        std::cout << "错误详情（最多 10 条）:\n";
        std::size_t limit = std::min<std::size_t>(result.errors.size(), 10);
        for (std::size_t i = 0; i < limit; ++i)
            std::cout << "  [" << result.errors[i].path << "] " << result.errors[i].message << "\n";
        if (result.errors.size() > limit)
            std::cout << "  ... 其余 " << (result.errors.size() - limit) << " 条已省略\n";
        std::cout << "\n";
    }
    std::cout << "扩展名统计:\n";
    for (const auto& [ext, count] : result.extension_counts)
        std::cout << "  " << std::setw(16) << std::left << ext << count << " 个\n";
}

template <typename Char>
int run_scanner(int argc, Char* argv[]) {
    CommandLineOptions opts;
    try { opts = parse_arguments(argc, argv); }
    catch (const std::exception& e) { std::cerr << "参数错误: " << e.what() << "\n"; print_usage(); return 1; }

    if (opts.show_help) { print_usage(); return 0; }

    // --list-scans: 列出历史记录
    if (opts.list_scans) {
        if (!opts.db_path) { std::cerr << "--list-scans 需要指定 --db\n"; return 1; }
        try {
            scanner::infrastructure::DatabaseStorage db(*opts.db_path);
            auto scans = db.list_scans(10);
            if (scans.empty()) { std::cout << "无扫描记录\n"; return 0; }
            std::cout << "ID | 时间 | 路径 | 文件数 | 大小(MiB) | 耗时(s)\n";
            std::cout << std::string(80, '-') << "\n";
            for (const auto& s : scans) {
                std::cout << s.scan_id.substr(0, 8) << " | " << s.scan_time << " | "
                          << s.root_path << " | " << s.file_count << " | "
                          << std::fixed << std::setprecision(2)
                          << static_cast<double>(s.total_bytes) / (1024.0 * 1024.0)
                          << " | " << s.elapsed_seconds << "\n";
            }
        } catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
        return 0;
    }

    try {
        // 扫描引擎 + 日志
        std::unique_ptr<scanner::infrastructure::ErrorLogger> error_logger;
        if (opts.log_path) error_logger = std::make_unique<scanner::infrastructure::ErrorLogger>(*opts.log_path);

        scanner::infrastructure::FileScanner file_scanner(error_logger.get());

        ScanOptions scan_opts;
        scan_opts.root = opts.root;
        scan_opts.worker_count = opts.worker_count;
        scan_opts.filter = opts.filter;

        auto result = file_scanner.scan(scan_opts);
        print_scan_result(result, opts.root, opts.worker_count);
        print_filter(opts.filter);
        std::cout << "\n";

        // CSV 导出
        if (opts.output_path) {
            scanner::infrastructure::CsvExporter().export_result(*opts.output_path, result);
            std::cout << "CSV: " << scanner::domain::display_path(*opts.output_path) << "\n";
        }

        // JSON 导出
        if (opts.json_path) {
            scanner::infrastructure::JsonExporter().export_result(*opts.json_path, result);
            std::cout << "JSON: " << scanner::domain::display_path(*opts.json_path) << "\n";
        }

        // 数据库保存
        if (opts.db_path) {
            scanner::infrastructure::DatabaseStorage db(*opts.db_path);
            auto scan_id = db.save_scan(result, scanner::domain::display_path(opts.root));
            std::cout << "数据库: " << scanner::domain::display_path(*opts.db_path)
                      << " (scan_id=" << scan_id.substr(0, 8) << "...)\n";
        }

        // 重复文件检测
        if (opts.show_dupes) {
            std::cout << "\n=== 重复文件检测 ===\n";
            scanner::infrastructure::DuplicateFinder finder;
            auto dupes = finder.find(opts.root, opts.worker_count);
            if (dupes.empty()) {
                std::cout << "未发现重复文件\n";
            } else {
                std::cout << "发现 " << dupes.size() << " 组重复文件:\n";
                for (const auto& group : dupes) {
                    std::cout << "  [" << (static_cast<double>(group.file_size) / 1024.0)
                              << " KB] x" << group.paths.size() << "\n";
                    for (const auto& p : group.paths)
                        std::cout << "    " << p << "\n";
                }
            }
        }

        // 扫描对比
        if (opts.compare_id && opts.db_path) {
            std::cout << "\n=== 扫描对比 ===\n";
            scanner::infrastructure::DatabaseStorage db(*opts.db_path);
            auto current_id = db.save_scan(result, scanner::domain::display_path(opts.root));
            auto changes = db.compare_scans(*opts.compare_id, current_id);
            int added = 0, removed = 0, modified = 0;
            for (const auto& c : changes) {
                switch (c.type) {
                    case scanner::infrastructure::FileChange::ADDED: ++added; break;
                    case scanner::infrastructure::FileChange::REMOVED: ++removed; break;
                    case scanner::infrastructure::FileChange::MODIFIED: ++modified; break;
                }
            }
            std::cout << "新增: " << added << "  删除: " << removed << "  修改: " << modified << "\n";
            for (const auto& c : changes) {
                const char* tag = c.type == scanner::infrastructure::FileChange::ADDED ? "+"
                                : c.type == scanner::infrastructure::FileChange::REMOVED ? "-" : "~";
                std::cout << "  " << tag << " " << c.path;
                if (c.type == scanner::infrastructure::FileChange::MODIFIED)
                    std::cout << " (" << c.old_size << " -> " << c.new_size << ")";
                std::cout << "\n";
            }
        }
    } catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }

    return 0;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) { return run_scanner(argc, argv); }
#else
int main(int argc, char* argv[]) { return run_scanner(argc, argv); }
#endif
