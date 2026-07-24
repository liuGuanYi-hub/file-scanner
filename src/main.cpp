#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "scan_engine.h"

namespace fs = std::filesystem;

namespace {

struct CommandLineOptions {
    fs::path root = fs::current_path();
    std::size_t worker_count = scanner::default_worker_count();
    scanner::FileFilter filter;
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
        return scanner::parse_extensions(std::wstring(value));
    } else {
        return scanner::parse_extensions(std::string(value));
    }
}

template <typename Char>
std::optional<std::uintmax_t> parse_size_argument(
    const std::basic_string<Char>& value) {
    if constexpr (std::is_same_v<Char, wchar_t>) {
        return scanner::parse_size_bytes(std::wstring(value));
    } else {
        return scanner::parse_size_bytes(std::string(value));
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

        if (consume_option_value(index, argc, argv, argument, "--path",
                                 [&](const auto& value) {
                                     options.root = fs::path(value);
                                     root_set = true;
                                 })) {
            continue;
        }
        if (consume_option_value(index, argc, argv, argument, "--threads",
                                 [&](const auto& value) {
                                     if constexpr (std::is_same_v<Char, wchar_t>) {
                                         options.worker_count = scanner::parse_worker_count(
                                             std::wstring(value));
                                     } else {
                                         options.worker_count = scanner::parse_worker_count(
                                             std::string(value));
                                     }
                                     worker_set = true;
                                 })) {
            continue;
        }
        if (consume_option_value(index, argc, argv, argument, "--ext",
                                 [&](const auto& value) {
                                     options.filter.extensions =
                                         parse_extensions_argument(value);
                                 })) {
            continue;
        }
        if (consume_option_value(index, argc, argv, argument, "--min-size",
                                 [&](const auto& value) {
                                     options.filter.min_size = parse_size_argument(value);
                                 })) {
            continue;
        }
        if (consume_option_value(index, argc, argv, argument, "--max-size",
                                 [&](const auto& value) {
                                     options.filter.max_size = parse_size_argument(value);
                                 })) {
            continue;
        }

        if (!argument.empty() && argument.front() == static_cast<Char>('-')) {
            throw std::invalid_argument("未知选项，请使用 --help 查看用法");
        }
        if (!root_set) {
            options.root = fs::path(argument);
            root_set = true;
        } else if (!worker_set) {
            if constexpr (std::is_same_v<Char, wchar_t>) {
                options.worker_count = scanner::parse_worker_count(std::wstring(argument));
            } else {
                options.worker_count = scanner::parse_worker_count(std::string(argument));
            }
            worker_set = true;
        } else {
            throw std::invalid_argument("位置参数过多，请使用 --help 查看用法");
        }
    }

    if (options.filter.min_size && options.filter.max_size &&
        *options.filter.min_size > *options.filter.max_size) {
        throw std::invalid_argument("最小文件大小不能大于最大文件大小");
    }
    return options;
}

void print_usage() {
    std::cout << "用法: file_scanner.exe [扫描目录] [线程数] [选项]\n"
              << "示例: file_scanner.exe . 4 --ext .cpp,.h\n"
              << "      file_scanner.exe --path . --threads 4 --min-size 1024\n\n"
              << "选项:\n"
              << "  --path <目录>       指定扫描目录\n"
              << "  --threads <数量>    指定工作线程数，范围 1-64\n"
              << "  --ext <列表>        扩展名过滤，例如 .cpp,.h\n"
              << "  --min-size <字节>   最小文件大小，单位为字节\n"
              << "  --max-size <字节>   最大文件大小，单位为字节\n"
              << "  --help              显示帮助信息\n";
}

void print_filter(const scanner::FileFilter& filter) {
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
        scanner::ScanOptions scan_options;
        scan_options.root = options.root;
        scan_options.worker_count = options.worker_count;
        scan_options.filter = options.filter;
        const scanner::ScanResult result = scanner::scan_directory(scan_options);

        std::cout << "\n扫描完成\n"
                  << "目录: " << scanner::display_path(options.root) << "\n"
                  << "线程数: " << options.worker_count << "\n"
                  << "文件数: " << result.file_count << "\n"
                  << "总大小: " << std::fixed << std::setprecision(2)
                  << static_cast<double>(result.total_bytes) / (1024.0 * 1024.0)
                  << " MiB\n"
                  << "错误数: " << result.error_count << "\n"
                  << "元数据记录: " << result.files.size() << " 条\n"
                  << "耗时: " << result.elapsed_seconds << " 秒\n\n";

        print_filter(options.filter);
        if (!options.filter.extensions.empty() || options.filter.min_size ||
            options.filter.max_size) {
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
