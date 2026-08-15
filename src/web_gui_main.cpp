// 现代 Web GUI 入口 — 嵌入式 HTTP 服务 + 浏览器 UI
#ifdef _WIN32

#ifndef UNICODE
#define UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shellapi.h>
#include <signal.h>
#include <io.h>
#include <fcntl.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>

#include "gui_server.cpp"  // 内联包含服务器实现
#include "scanner/domain/types.h"
#include "scanner/domain/utilities.h"
#include "scanner/infrastructure/file_scanner.h"
#include "scanner/infrastructure/csv_exporter.h"
#include "scanner/infrastructure/json_exporter.h"
#include "scanner/infrastructure/duplicate_finder.h"
#include "scanner/infrastructure/database_storage.h"

namespace fs = std::filesystem;
using namespace scanner;

// 在浏览器中打开 URL
void open_browser(const std::string& url) {
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOW);
}

// URL 解码（%XX → 字符，+ → 空格）
std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = s.substr(i + 1, 2);
            try {
                out += static_cast<char>(std::stoi(hex, nullptr, 16));
            } catch (...) {
                out += s[i];
            }
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

// 解析查询参数
std::string query_param(const std::string& query, const std::string& key) {
    auto pos = query.find(key + "=");
    if (pos == std::string::npos) return {};
    auto start = pos + key.size() + 1;
    auto end = query.find('&', start);
    return url_decode(query.substr(start, end == std::string::npos ? query.size() - start : end - start));
}

// JSON 字符串转义（Windows 路径含反斜杠，必须转义）
std::string json_escape(const std::string& s) {
    std::ostringstream out;
    for (char c : s) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"':  out << "\\\""; break;
            case '\n': out << "\\n";  break;
            case '\r': out << "\\r";  break;
            case '\t': out << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(c))
                        << std::dec;
                } else {
                    out << c;
                }
        }
    }
    return out.str();
}

// API: /api/scan — 执行文件扫描
std::string handle_scan(const scanner::server::MiniHttpServer::Request& req) {
    auto path_str = query_param(req.query, "path");
    auto threads_str = query_param(req.query, "threads");
    auto ext_str = query_param(req.query, "ext");

    domain::ScanOptions opts;
    opts.root = path_str.empty() ? fs::current_path() : fs::path(path_str);
    opts.worker_count = domain::default_worker_count();
    if (!threads_str.empty()) {
        try { opts.worker_count = domain::parse_worker_count(threads_str); }
        catch (...) {}
    }
    if (!ext_str.empty()) {
        opts.filter.extensions = domain::parse_extensions(ext_str);
    }

    infrastructure::FileScanner scanner(nullptr);
    auto result = scanner.scan(opts);

    // 构建 JSON 响应
    std::ostringstream json;
    json << "{";
    json << "\"file_count\":" << result.file_count << ",";
    json << "\"total_bytes\":" << result.total_bytes << ",";
    json << "\"error_count\":" << result.error_count << ",";
    json << "\"elapsed_seconds\":" << result.elapsed_seconds << ",";
    json << "\"cancelled\":" << (result.cancelled ? "true" : "false") << ",";

    // 扩展名
    json << "\"extensions\":{";
    bool first = true;
    for (const auto& [ext, count] : result.extension_counts) {
        if (!first) json << ",";
        json << "\"" << ext << "\":" << count;
        first = false;
    }
    json << "},";

    // 文件列表（最多返回 200 条）
    json << "\"files\":[";
    size_t limit = std::min(result.files.size(), size_t(200));
    for (size_t i = 0; i < limit; ++i) {
        if (i > 0) json << ",";
        const auto& f = result.files[i];
        json << "{";
        json << "\"path\":\"" << json_escape(f.path) << "\",";
        json << "\"size\":" << f.size << ",";
        json << "\"ext\":\"" << json_escape(f.extension) << "\"";
        json << "}";
    }
    json << "],";

    // 错误
    json << "\"errors\":[";
    for (size_t i = 0; i < result.errors.size(); ++i) {
        if (i > 0) json << ",";
        json << "{\"path\":\"" << json_escape(result.errors[i].path)
             << "\",\"message\":\"" << json_escape(result.errors[i].message) << "\"}";
    }
    json << "]";
    json << "}";

    return json.str();
}

// 全局日志到文件（因为这是 WIN32 子系统，无控制台）
static std::ofstream g_log;
static void log(const std::string& msg) {
    if (g_log.is_open()) {
        g_log << msg << std::endl;
        g_log.flush();
    }
    // 也尝试输出到 stdout（如果有控制台的话）
    std::cout << msg << std::endl;
}

static scanner::server::MiniHttpServer* g_server = nullptr;
BOOL WINAPI console_handler(DWORD) {
    if (g_server) g_server->stop();
    ExitProcess(0);
    return TRUE;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // 创建日志文件（写到 build 目录旁边）
    char exe_path[MAX_PATH];
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    std::filesystem::path log_path = std::filesystem::path(exe_path).parent_path() / "web_gui.log";
    g_log.open(log_path.u8string(), std::ios::out | std::ios::trunc);
    SetConsoleCtrlHandler(console_handler, TRUE);

    log("=== 文件扫描器 Web GUI 启动 ===");
    log("日志文件: " + log_path.u8string());

    // 获取项目根目录（启动目录）
    fs::path project_root = fs::current_path();
    fs::path gui_dir = project_root / "gui";

    if (!fs::exists(gui_dir / "index.html")) {
        // 尝试相对于 exe 位置
        fs::path exe_dir = fs::path(exe_path).parent_path();
        gui_dir = exe_dir / "gui";
        if (!fs::exists(gui_dir / "index.html")) {
            gui_dir = exe_dir.parent_path() / "gui";
        }
    }

    log("静态文件目录: " + gui_dir.u8string());

    if (!fs::exists(gui_dir / "index.html")) {
        log("错误: 找不到 gui/index.html");
        MessageBoxW(nullptr, L"找不到 gui/index.html，请到项目根目录运行",
                    L"文件扫描器 Web GUI", MB_OK | MB_ICONERROR);
        return 1;
    }

    g_server = new scanner::server::MiniHttpServer();
    g_server->serve_static("/", gui_dir);
    g_server->get("/api/scan", handle_scan);

    // 尝试启动，端口冲突时递增
    int port = 8899;
    for (int i = 0; i < 10; ++i) {
        if (g_server->start(port)) {
            log("服务器已启动: http://localhost:" + std::to_string(port));
            break;
        }
        log("端口 " + std::to_string(port) + " 占用，尝试下一个");
        port++;
    }

    if (port >= 8909) {
        log("错误: 所有尝试端口均被占用");
        MessageBoxW(nullptr, L"所有尝试端口 (8899-8908) 均被占用",
                    L"文件扫描器 Web GUI", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::string url = "http://localhost:" + std::to_string(port);
    log("打开浏览器: " + url);
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOW);

    // 弹出消息框告诉用户服务已启动（用 UTF-16 中文）
    std::wstring msg = L"文件扫描器 Web GUI 已启动！\n\n地址: " +
                       std::wstring(url.begin(), url.end()) +
                       L"\n\n(关闭此窗口以停止服务)";
    MessageBoxW(nullptr, msg.c_str(), L"文件扫描器 Web GUI", MB_OK | MB_ICONINFORMATION);

    log("Web GUI 已关闭");
    g_server->stop();
    delete g_server;
    g_server = nullptr;
    g_log.close();
    return 0;
}

#endif  // _WIN32
