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
#include "scanner/infrastructure/error_logger.h"
#include "scanner/infrastructure/file_scanner.h"

namespace fs = std::filesystem;
using namespace scanner::domain;

namespace {

struct CommandLineOptions {
    fs::path root = fs::current_path();
    std::size_t worker_count = scanner::domain::default_worker_count();
    FileFilter filter;
    std::optional<fs::path> log_path;
    std::optional<fs::path> output_path;
    bool show_help = false;
};

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

    const std::basic_string<Char> prefix =
        option + make_text<Char>("=");
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
        if (consume_option_value(index, argc, argv, argument, name,
                                 callback)) {
            return true;
        }
    }
    return false;
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

        if (consume_option_aliases(
                index, argc, argv, argument, {"--path", "-p"},
                [&](const auto& value) {
                    require_option_value(value, "--path");
                    options.root = fs::path(value);
                    root_set = true;
                })) {
            continue;
        }
        if (consume_option_aliases(
                index, argc, argv, argument, {"--threads", "-t"},
                [&](const auto& value) {
                    if constexpr (std::is_same_v<Char, wchar_t>) {
                        options.worker_count =
                            scanner::domain::parse_worker_count(
                                std::wstring(value));
                    } else {
                        options.worker_count =
                            scanner::domain::parse_worker_count(
                                std::string(value));
                    }
                    worker_set = true;
                })) {
            continue;
        }
        if (consume_option_aliases(
                index, argc, argv, argument, {"--ext", "-e"},
                [&](const auto& value) {
                    require_option_value(value, "--ext");
                    options.filter.extensions =
                        parse_extensions_argument(value);
                })) {
            continue;
        }
        if (consume_option_aliases(
                index, argc, argv, argument, {"--min-size", "-m"},
                [&](const auto& value) {
                    options.filter.min_size =
                        parse_size_argument(value);
                })) {
            continue;
        }
        if (consume_option_aliases(
                index, argc, argv, argument, {"--max-size", "-M"},
                [&](const auto& value) {
                    options.filter.max_size =
                        parse_size_argument(value);
                })) {
            continue;
        }
        if (consume_option_aliases(
                index, argc, argv, argument, {"--log", "-l"},
                [&](const auto& value) {
                    require_option_value(value, "--log");
                    options.log_path = fs::path(value);
                })) {
            continue;
        }
        if (consume_option_aliases(
                index, argc, argv, argument, {"--output", "-o"},
                [&](const auto& value) {
                    require_option_value(value, "--output");
                    options.output_path = fs::path(value);
                })) {
            continue;
        }

        if (!argument.empty() &&
            argument.front() == static_cast<Char>('-')) {
            throw std::invalid_argument("未知选项，请使用 --help 查看用法");
        }
        if (!root_set) {
            options.root = fs::path(argument);
            root_set = true;
        } else if (!worker_set) {
            if constexpr (std::is_same_v<Char, wchar_t>) {
                options.worker_count =
                    scanner::domain::parse_worker_count(
                        std::wstring(argument));
            } else {
                options.worker_count =
                    scanner::domain::parse_worker_count(
                        std::string(argument));
            }
            worker_set = true;
        } else {
            throw std::invalid_argument(
                "位置参数过多，请使用 --help 查看用法");
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
        << "用法: file_scanner.exe [扫描目录] [线程数] [选项]\n"
        << "示例: file_scanner.exe . 4 --ext .cpp,.h\n"
        << "      file_scanner.exe --path . --threads 4 --output output\\scan.csv\n"
        << "      file_scanner.exe -p . -t 4 -e cpp,h -o output\\scan.csv\n\n"
        << "选项:\n"
        << "  --path, -p <目录>       指定扫描目录，默认当前目录\n"
        << "  --threads, -t <数量>    工作线程数，范围 1-64，默认按 CPU 自动选择\n"
        << "  --ext, -e <列表>        扩展名过滤，例如 .cpp,.h\n"
        << "  --min-size, -m <字节>   最小文件大小，单位为字节\n"
        << "  --max-size, -M <字节>   最大文件大小，单位为字节\n"
        << "  --log, -l <文件>        将错误写入指定日志文件\n"
        << "  --output, -o <文件>     将匹配文件元数据导出为 CSV\n"
        << "  --help, -h              显示帮助信息\n";
}

void print_filter(const FileFilter& filter) {
    if (filter.extensions.empty() && !filter.min_size && !filter.max_size) {
        return;
    }

    std::cout << "过滤条件:\n";
    if (!filter.extensions.empty()) {
        std::cout << "  扩展名: ";
        bool first = true;
        for (const auto& extension : filter.extensions) {
            if (!first) {
                std::cout << ",";
            }
            std::cout << extension;
            first = false;
        }
        std::cout << "\n";
    }
    if (filter.min_size) {
        std::cout << "  最小大小: " << *filter.min_size << " 字节\n";
    }
    if (filter.max_size) {
        std::cout << "  最大大小: " << *filter.max_size << " 字节\n";
    }
}

template <typename Char>
int run_scanner(int argc, Char* argv[]) {
    CommandLineOptions options;
    try {
        options = parse_arguments(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "参数错误: " << error.what() << "\n";
        print_usage();
        return 1;
    }

    if (options.show_help) {
        print_usage();
        return 0;
    }

    try {
        // 使用依赖注入：通过构造函数将 logger 注入 FileScanner
        std::unique_ptr<scanner::infrastructure::ErrorLogger> error_logger;
        if (options.log_path) {
            error_logger =
                std::make_unique<scanner::infrastructure::ErrorLogger>(
                    *options.log_path);
        }

        scanner::infrastructure::FileScanner file_scanner(
            error_logger.get());
        scanner::infrastructure::CsvExporter csv_exporter;

        ScanOptions scan_options;
        scan_options.root = options.root;
        scan_options.worker_count = options.worker_count;
        scan_options.filter = options.filter;

        const ScanResult result = file_scanner.scan(scan_options);

        if (options.output_path) {
            csv_exporter.export_result(*options.output_path, result);
        }

        std::cout << "\n扫描完成\n"
                  << "目录: " << scanner::domain::display_path(options.root)
                  << "\n"
                  << "线程数: " << options.worker_count << "\n"
                  << "文件数: " << result.file_count << "\n"
                  << "总大小: " << std::fixed << std::setprecision(2)
                  << static_cast<double>(result.total_bytes) /
                         (1024.0 * 1024.0)
                  << " MiB\n"
                  << "错误数: " << result.error_count << "\n"
                  << "元数据记录: " << result.files.size() << " 条\n"
                  << "耗时: " << result.elapsed_seconds << " 秒\n\n";

        if (!result.errors.empty()) {
            std::cout << "错误详情（最多显示 10 条）:\n";
            const std::size_t limit =
                result.errors.size() < 10 ? result.errors.size() : 10;
            for (std::size_t index = 0; index < limit; ++index) {
                std::cout << "  [" << result.errors[index].path << "] "
                          << result.errors[index].message << "\n";
            }
            if (result.errors.size() > limit) {
                std::cout << "  ... 其余 "
                          << (result.errors.size() - limit)
                          << " 条错误已省略\n";
            }
            std::cout << "\n";
        }
        if (options.log_path) {
            std::cout << "错误日志: "
                      << scanner::domain::display_path(*options.log_path)
                      << "\n";
        }
        if (result.log_write_failures > 0) {
            std::cout << "日志写入失败数: "
                      << result.log_write_failures << "\n";
        }
        if (options.output_path) {
            std::cout
                << "CSV 文件: "
                << scanner::domain::display_path(*options.output_path)
                << "\n"
                << "导出记录: " << result.files.size() << " 条\n";
        }

        print_filter(options.filter);
        if (!options.filter.extensions.empty() ||
            options.filter.min_size || options.filter.max_size) {
            std::cout << "\n";
        }
        std::cout << "扩展名统计:\n";
        for (const auto& [extension, count] : result.extension_counts) {
            std::cout << "  " << std::setw(16) << std::left << extension
                      << count << " 个\n";
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }

    return 0;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    return run_scanner(argc, argv);
}
#else
int main(int argc, char* argv[]) {
    return run_scanner(argc, argv);
}
#endif
