#include "socketcast/admin_server.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace socketcast {

AdminServer::AdminServer(uint16_t port) : port_(port) {}

AdminServer::~AdminServer() { stop(); }

bool AdminServer::start() {
    if (running_.load()) {
        return false;
    }

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::cerr << "AdminServer: socket failed\n";
        return false;
    }

    int reuse = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "AdminServer: setsockopt SO_REUSEADDR failed\n";
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        addr.sin_addr.s_addr = INADDR_LOOPBACK;
    }

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "AdminServer: bind failed on port " << port_ << "\n";
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, kBacklog) < 0) {
        std::cerr << "AdminServer: listen failed\n";
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    running_ = true;
    server_thread_ = std::make_unique<std::thread>(&AdminServer::run_server, this);
    std::cout << "AdminServer listening on 127.0.0.1:" << port_ << "\n";
    return true;
}

void AdminServer::stop() {
    if (!running_.load()) {
        return;
    }
    running_ = false;
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (server_thread_ && server_thread_->joinable()) {
        server_thread_->join();
    }
}

void AdminServer::run_server() {
    while (running_.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (running_.load()) {
                std::cerr << "AdminServer: accept failed\n";
            }
            continue;
        }

        handle_connection(client_fd);
    }
}

void AdminServer::handle_connection(int client_fd) {
    char buffer[kBufferSize];
    ssize_t nbytes = recv(client_fd, buffer, kBufferSize - 1, 0);
    if (nbytes <= 0) {
        close(client_fd);
        return;
    }
    buffer[nbytes] = '\0';

    // Parse HTTP request (simple parser)
    std::istringstream iss(buffer);
    std::string method, path, http_version;
    iss >> method >> path >> http_version;

    // Find empty line (end of headers)
    std::string line;
    std::string body;
    bool in_body = false;
    while (std::getline(iss, line)) {
        if (line.empty() || line == "\r") {
            in_body = true;
            break;
        }
    }
    if (in_body) {
        std::getline(iss, body);
    }

    // Handle request
    std::string response = handle_request(method, path, body);

    // Send response
    send(client_fd, response.c_str(), response.length(), 0);
    close(client_fd);
}

std::string AdminServer::handle_request(const std::string& method, const std::string& path,
                                        const std::string& body) {
    // GET /admin/health
    if (method == "GET" && path == "/admin/health") {
        return http_response(200, R"({"status":"ok"})");
    }

    // POST /admin/streams (start stream)
    if (method == "POST" && path == "/admin/streams") {
        if (!start_stream_cb_) {
            return http_response(500, json_error("no start stream callback"));
        }
        // Parse JSON body (minimal: {"input":"...", "host":"...", "port":5000, "fps":30})
        StreamRequest req;
        // TODO: proper JSON parsing; for now use simple parsing
        if (body.find("\"input\"") != std::string::npos) {
            size_t start = body.find("\"input\"") + 9;
            size_t end = body.find("\"", start + 1);
            if (start < body.length() && end < body.length()) {
                req.input = body.substr(start + 1, end - start - 1);
            }
        }
        if (body.find("\"host\"") != std::string::npos) {
            size_t start = body.find("\"host\"") + 8;
            size_t end = body.find("\"", start + 1);
            if (start < body.length() && end < body.length()) {
                req.host = body.substr(start + 1, end - start - 1);
            }
        }
        if (req.host.empty()) {
            req.host = "127.0.0.1";
        }
        if (body.find("\"port\"") != std::string::npos) {
            size_t start = body.find("\"port\"") + 7;
            try {
                req.port = static_cast<uint16_t>(std::stoi(body.substr(start)));
            } catch (...) {
                req.port = 5000;
            }
        }

        std::string stream_id = start_stream_cb_(req);
        if (stream_id.empty()) {
            return http_response(400, json_error("failed to start stream"));
        }
        return http_response(200, R"({"stream_id":")" + stream_id + R"("})");
    }

    // DELETE /admin/streams/{id}
    if (method == "DELETE" && path.substr(0, 16) == "/admin/streams/") {
        if (!stop_stream_cb_) {
            return http_response(500, json_error("no stop stream callback"));
        }
        std::string stream_id = path.substr(16);
        bool ok = stop_stream_cb_(stream_id);
        if (!ok) {
            return http_response(404, json_error("stream not found"));
        }
        return http_response(200, R"({"status":"stopped"})");
    }

    // GET /admin/streams/{id}/stats
    if (method == "GET" && path.substr(0, 15) == "/admin/streams/" &&
        path.find("/stats") != std::string::npos) {
        if (!get_stats_cb_) {
            return http_response(500, json_error("no get stats callback"));
        }
        size_t id_start = 15;
        size_t id_end = path.find("/stats");
        if (id_start >= id_end) {
            return http_response(400, json_error("invalid path"));
        }
        std::string stream_id = path.substr(id_start, id_end - id_start);
        StreamStats stats;
        bool ok = get_stats_cb_(stream_id, stats);
        if (!ok) {
            return http_response(404, json_error("stream not found"));
        }
        std::ostringstream oss;
        oss << R"({"rtt_ms":)" << stats.rtt_ms << R"(,"jitter_ms":)" << stats.jitter_ms
            << R"(,"frames_received":)" << stats.frames_received << R"(,"packets_received":)"
            << stats.packets_received << R"(,"bytes_received":)" << stats.bytes_received
            << R"(,"data_sent":)" << stats.data_sent << "}";
        return http_response(200, oss.str());
    }

    return http_response(404, json_error("not found"));
}

std::string AdminServer::http_response(int status_code, const std::string& body) {
    std::ostringstream oss;
    const char* status_text = (status_code == 200) ? "OK"
                               : (status_code == 400) ? "Bad Request"
                               : (status_code == 404) ? "Not Found"
                               : (status_code == 500) ? "Internal Server Error"
                               : "Unknown";
    oss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    oss << "Content-Type: application/json\r\n";
    oss << "Content-Length: " << body.length() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}

std::string AdminServer::json_error(const std::string& message) {
    return R"({"error":")" + message + R"("})";
}

}  // namespace socketcast
