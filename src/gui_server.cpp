// 微型 HTTP 服务器 — 为现代化 Web UI 提供后端服务
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace scanner::server {

namespace fs = std::filesystem;

class MiniHttpServer {
public:
    struct Request {
        std::string method;
        std::string path;
        std::string body;
        std::string query;
    };

    using Handler = std::function<std::string(const Request&)>;

    MiniHttpServer() {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }

    ~MiniHttpServer() {
        stop();
        WSACleanup();
    }

    void get(const std::string& path, Handler handler) {
        routes_.push_back({path, std::move(handler)});
    }

    void serve_static(const std::string& url_prefix, const fs::path& dir) {
        static_dir_ = dir;
        static_prefix_ = url_prefix;
    }

    bool start(int port) {
        port_ = port;
        listen_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_sock_ == INVALID_SOCKET) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<u_short>(port_));

        if (bind(listen_sock_, reinterpret_cast<sockaddr*>(&addr),
                 sizeof(addr)) != 0) {
            closesocket(listen_sock_);
            listen_sock_ = INVALID_SOCKET;
            return false;
        }

        listen(listen_sock_, 5);
        running_ = true;
        thread_ = std::thread(&MiniHttpServer::accept_loop, this);
        return true;
    }

    void stop() {
        running_ = false;
        if (listen_sock_ != INVALID_SOCKET) {
            closesocket(listen_sock_);
            listen_sock_ = INVALID_SOCKET;
        }
        if (thread_.joinable()) thread_.join();
    }

    int port() const { return port_; }

private:
    std::string read_file(const fs::path& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return {};
        return std::string(std::istreambuf_iterator<char>(f),
                           std::istreambuf_iterator<char>());
    }

    std::string build_response(int code, const std::string& content,
                                const std::string& content_type = "text/html") {
        std::ostringstream resp;
        resp << "HTTP/1.1 " << code << " OK\r\n"
             << "Content-Type: " << content_type << "; charset=utf-8\r\n"
             << "Content-Length: " << content.size() << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Connection: close\r\n\r\n"
             << content;
        return resp.str();
    }

    void handle_client(SOCKET client) {
        // 设置接收超时，防止客户端连接后不发数据导致单线程服务器卡死
        DWORD recv_timeout_ms = 5000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&recv_timeout_ms),
                   sizeof(recv_timeout_ms));
        char buf[8192]{};
        int n = recv(client, buf, sizeof(buf) - 1, 0);
        if (n <= 0) { closesocket(client); return; }

        // 解析 HTTP 请求
        std::string raw(buf, n);
        Request req;
        auto first_line_end = raw.find("\r\n");
        if (first_line_end == std::string::npos) {
            closesocket(client); return;
        }
        auto first_line = raw.substr(0, first_line_end);

        // METHOD PATH
        auto sp1 = first_line.find(' ');
        auto sp2 = first_line.find(' ', sp1 + 1);
        if (sp1 == std::string::npos || sp2 == std::string::npos) {
            closesocket(client); return;
        }
        req.method = first_line.substr(0, sp1);
        auto full_path = first_line.substr(sp1 + 1, sp2 - sp1 - 1);

        auto qm = full_path.find('?');
        if (qm != std::string::npos) {
            req.path = full_path.substr(0, qm);
            req.query = full_path.substr(qm + 1);
        } else {
            req.path = full_path;
        }

        // 路由匹配
        std::string response;
        for (const auto& [route, handler] : routes_) {
            if (req.path == route) {
                try {
                    response = build_response(200, handler(req),
                                              route.find("/api/") == 0
                                                  ? "application/json"
                                                  : "text/html");
                } catch (...) {
                    response = build_response(500, "{\"error\":\"internal\"}",
                                             "application/json");
                }
                send(client, response.c_str(),
                     static_cast<int>(response.size()), 0);
                closesocket(client);
                return;
            }
        }

        // 静态文件
        if (!static_dir_.empty() && req.path.rfind(static_prefix_, 0) == 0) {
            auto relative = req.path.substr(static_prefix_.size());
            // 去掉前导 '/'，避免 operator/ 把相对路径当成绝对路径整体替换
            while (!relative.empty() && relative.front() == '/') {
                relative.erase(relative.begin());
            }
            if (relative.empty()) relative = "index.html";
            auto content = read_file(static_dir_ / relative);
            if (!content.empty()) {
                std::string ct = "text/html";
                if (relative.size() > 3 && relative.compare(relative.size() - 3, 3, ".js") == 0)
                    ct = "application/javascript";
                else if (relative.size() > 4 && relative.compare(relative.size() - 4, 4, ".css") == 0)
                    ct = "text/css";
                else if (relative.size() > 4 && relative.compare(relative.size() - 4, 4, ".png") == 0)
                    ct = "image/png";
                response = build_response(200, content, ct);
            } else {
                response = build_response(404, "Not Found");
            }
        } else {
            response = build_response(404, "Not Found");
        }

        send(client, response.c_str(),
             static_cast<int>(response.size()), 0);
        closesocket(client);
    }

    void accept_loop() {
        // 监听 socket 已在 start() 中完成 socket/bind/listen，
        // 这里只负责 accept 循环，避免重复初始化导致 bind 冲突。
        while (running_) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(listen_sock_, &fds);
            timeval tv{1, 0};
            if (select(0, &fds, nullptr, nullptr, &tv) <= 0) continue;

            SOCKET client = accept(listen_sock_, nullptr, nullptr);
            if (client != INVALID_SOCKET) {
                // 每个连接独立线程处理，避免单线程 accept 循环被阻塞
                // （浏览器 keep-alive 保持连接会卡死串行处理）
                std::thread(&MiniHttpServer::handle_client, this, client)
                    .detach();
            }
        }
    }

    struct Route {
        std::string path;
        Handler handler;
    };

    std::vector<Route> routes_;
    fs::path static_dir_;
    std::string static_prefix_;
    std::thread thread_;
    SOCKET listen_sock_ = INVALID_SOCKET;
    int port_ = 8899;
    std::atomic_bool running_{false};
};

}  // namespace scanner::server

#endif  // _WIN32
