#include "scanner/infrastructure/tool_factories.h"

#include <algorithm>
#include <filesystem>
#include <sstream>

#include "scanner/application/i_scan_tool.h"
#include "scanner/infrastructure/duplicate_finder.h"
#include "scanner/infrastructure/file_scanner.h"
#include "scanner/domain/types.h"

namespace scanner {
namespace infrastructure {

namespace {

namespace fs = std::filesystem;

using application::IScanTool;
using application::ToolContext;
using application::ToolOutput;

// === 目录扫描工具 ===
class ScanToolImpl : public IScanTool {
public:
    std::string name() const override { return "scan"; }
    std::string description() const override {
        return "递归扫描目录，统计文件数量与大小";
    }

    ToolOutput run(const ToolContext& ctx) override {
        ToolOutput out;
        FileScanner scanner(nullptr);
        domain::ScanOptions opts;
        opts.root = ctx.root;
        opts.worker_count = ctx.worker_count;
        auto result = scanner.scan(opts);

        std::ostringstream oss;
        oss << "文件数: " << result.file_count
            << " | 总大小: " << result.total_bytes << " 字节"
            << " | 错误: " << result.error_count
            << " | 耗时: " << result.elapsed_seconds << "s";
        out.summary = oss.str();
        out.exit_code = 0;
        return out;
    }
};

// === 重复文件检测工具 ===
class DuplicatesToolImpl : public IScanTool {
public:
    std::string name() const override { return "dupes"; }
    std::string description() const override {
        return "检测重复文件（支持 --extra=name-size 快速模式）";
    }

    ToolOutput run(const ToolContext& ctx) override {
        ToolOutput out;
        DuplicateFinder finder;
        auto mode = DuplicateMode::Content;
        if (ctx.extra.find("name-size") != std::string::npos) {
            mode = DuplicateMode::NameAndSize;
        }
        auto groups = finder.find(ctx.root, ctx.worker_count, mode);

        std::size_t dup_count = 0;
        for (const auto& g : groups) dup_count += g.paths.size();
        std::ostringstream oss;
        oss << "重复组: " << groups.size() << " 组"
            << " | 涉及文件: " << dup_count << " 个";
        out.summary = oss.str();
        for (const auto& g : groups) {
            std::ostringstream item;
            item << "[hash " << g.hash.substr(0, 12) << "]";
            for (const auto& p : g.paths) item << " " << p;
            out.items.push_back(item.str());
        }
        out.exit_code = 0;
        return out;
    }
};

// === 大文件扫描工具（全新工具示例） ===
class BigFilesToolImpl : public IScanTool {
public:
    explicit BigFilesToolImpl(std::uintmax_t min_size)
        : min_size_(min_size) {}

    std::string name() const override { return "big-files"; }
    std::string description() const override {
        return "列出大于指定大小的文件";
    }

    ToolOutput run(const ToolContext& ctx) override {
        ToolOutput out;
        std::vector<std::pair<fs::path, std::uintmax_t>> big;

        std::error_code ec;
        for (fs::recursive_directory_iterator it(
                 ctx.root, fs::directory_options::skip_permission_denied, ec),
             end;
             it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            std::error_code f_ec;
            if (it->is_regular_file(f_ec)) {
                std::error_code s_ec;
                auto size = fs::file_size(it->path(), s_ec);
                if (!s_ec && size >= min_size_) {
                    big.emplace_back(it->path(), size);
                }
            }
        }
        std::sort(big.begin(), big.end(),
                  [](const auto& a, const auto& b) {
                      return a.second > b.second;
                  });

        std::ostringstream oss;
        oss << "大文件(≥" << min_size_ << "B): " << big.size() << " 个";
        out.summary = oss.str();
        for (const auto& [p, size] : big) {
            out.items.push_back(p.u8string() + " (" +
                                std::to_string(size) + "B)");
        }
        out.exit_code = 0;
        return out;
    }

private:
    std::uintmax_t min_size_;
};

}  // namespace

std::shared_ptr<IScanTool> make_scan_tool() {
    return std::make_shared<ScanToolImpl>();
}

std::shared_ptr<IScanTool> make_duplicates_tool() {
    return std::make_shared<DuplicatesToolImpl>();
}

std::shared_ptr<IScanTool> make_big_files_tool(std::uintmax_t min_size) {
    return std::make_shared<BigFilesToolImpl>(min_size);
}

}  // namespace infrastructure
}  // namespace scanner
