#include "socketcast/admin_server.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/time.h>
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
    addr.sin_addr.s_addr = INADDR_ANY;

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
    std::cout << "AdminServer listening on 0.0.0.0:" << port_ << "\n";
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
    // Set socket timeout for graceful shutdown (1 second)
    struct timeval tv{};
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(listen_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (running_.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            // Timeout or error; continue checking running_ flag
            continue;
        }

        // One thread per connection: POST /admin/streams blocks for seconds
        // (synchronous transcode), which would otherwise stall every other
        // admin endpoint — including the frame/audio polling the dashboard needs.
        std::thread(&AdminServer::handle_connection, this, client_fd).detach();
    }
}

void AdminServer::handle_connection(int client_fd) {
    // Accepted sockets inherit listen RCVTIMEO; assemble a full request by
    // honoring Content-Length (httpx often delivers headers/body in two reads).
    struct timeval tv{};
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::string raw;
    char buffer[kBufferSize];
    size_t header_sep = std::string::npos;
    size_t sep_len = 4;
    size_t content_length = 0;
    bool have_length = false;

    while (raw.size() < static_cast<size_t>(kBufferSize)) {
        const ssize_t nbytes = recv(client_fd, buffer, sizeof(buffer), 0);
        if (nbytes <= 0) {
            break;
        }
        raw.append(buffer, static_cast<size_t>(nbytes));

        if (header_sep == std::string::npos) {
            size_t pos = raw.find("\r\n\r\n");
            sep_len = 4;
            if (pos == std::string::npos) {
                pos = raw.find("\n\n");
                sep_len = 2;
            }
            if (pos != std::string::npos) {
                header_sep = pos;
                const std::string headers = raw.substr(0, header_sep);
                size_t cl_pos = headers.find("Content-Length:");
                if (cl_pos == std::string::npos) {
                    cl_pos = headers.find("content-length:");
                }
                if (cl_pos != std::string::npos) {
                    size_t v = cl_pos + 15;
                    while (v < headers.size() && (headers[v] == ' ' || headers[v] == '\t')) {
                        ++v;
                    }
                    try {
                        content_length = static_cast<size_t>(std::stoul(headers.substr(v)));
                        have_length = true;
                    } catch (...) {
                        have_length = false;
                    }
                }
            }
        }

        if (header_sep != std::string::npos) {
            const size_t body_start = header_sep + sep_len;
            const size_t body_have = raw.size() > body_start ? raw.size() - body_start : 0;
            if (!have_length || body_have >= content_length) {
                break;
            }
        }
    }

    if (header_sep == std::string::npos) {
        std::string response = http_response(400, json_error("missing headers/body separator"));
        send(client_fd, response.c_str(), response.length(), 0);
        close(client_fd);
        return;
    }

    std::string headers = raw.substr(0, header_sep);
    std::string body = raw.substr(header_sep + sep_len);
    if (have_length && body.size() > content_length) {
        body.resize(content_length);
    }

    std::istringstream iss(headers);
    std::string method, path, http_version;
    if (!(iss >> method >> path >> http_version)) {
        std::string response = http_response(400, json_error("invalid request line"));
        send(client_fd, response.c_str(), response.length(), 0);
        close(client_fd);
        return;
    }

    std::string response = handle_request(method, path, body);
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
        // Parse JSON body (tolerates whitespace after ':' like Python json.dumps)
        StreamRequest req;
        auto skip_ws = [](const std::string& s, size_t i) -> size_t {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
                ++i;
            }
            return i;
        };
        auto extract_string = [&body, &skip_ws](const char* key) -> std::string {
            std::string search = std::string("\"") + key + "\"";
            size_t pos = body.find(search);
            if (pos == std::string::npos) {
                return "";
            }
            size_t i = skip_ws(body, pos + search.length());
            if (i >= body.size() || body[i] != ':') {
                return "";
            }
            i = skip_ws(body, i + 1);
            if (i >= body.size() || body[i] != '"') {
                return "";
            }
            size_t start = i + 1;
            size_t end = body.find('"', start);
            if (end == std::string::npos || end <= start) {
                return "";
            }
            return body.substr(start, end - start);
        };
        auto extract_number = [&body, &skip_ws](const char* key) -> int {
            std::string search = std::string("\"") + key + "\"";
            size_t pos = body.find(search);
            if (pos == std::string::npos) {
                return -1;
            }
            size_t i = skip_ws(body, pos + search.length());
            if (i >= body.size() || body[i] != ':') {
                return -1;
            }
            i = skip_ws(body, i + 1);
            size_t end = i;
            while (end < body.size() && (body[end] == '-' || (body[end] >= '0' && body[end] <= '9'))) {
                ++end;
            }
            if (end <= i) {
                return -1;
            }
            try {
                return std::stoi(body.substr(i, end - i));
            } catch (...) {
                return -1;
            }
        };

        req.input = extract_string("input");
        req.host = extract_string("host");
        if (req.host.empty()) {
            req.host = "127.0.0.1";
        }
        int port_val = extract_number("port");
        if (port_val > 0) {
            req.port = static_cast<uint16_t>(port_val);
        }
        int fps_val = extract_number("fps");
        if (fps_val > 0) {
            req.fps = static_cast<uint32_t>(fps_val);
        }

        if (req.input.empty()) {
            return http_response(400, json_error("missing input"));
        }

        std::string stream_id = start_stream_cb_(req);
        if (stream_id.empty()) {
            return http_response(400, json_error("failed to start stream"));
        }
        return http_response(200, R"({"stream_id":")" + stream_id + R"("})");
    }

    // DELETE /admin/streams/{id}
    // Prefix is 15 chars: "/admin/streams/" — do not use 16 (never matches).
    if (method == "DELETE" && path.rfind("/admin/streams/", 0) == 0 &&
        path.find("/stats") == std::string::npos) {
        if (!stop_stream_cb_) {
            return http_response(500, json_error("no stop stream callback"));
        }
        std::string stream_id = path.substr(15);  // after "/admin/streams/"
        if (stream_id.empty() || stream_id.find('/') != std::string::npos) {
            return http_response(400, json_error("invalid stream id"));
        }
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

    // GET /admin/frames (retrieve accumulated frames as base64)
    if (method == "GET" && path == "/admin/frames") {
        if (!get_frames_cb_) {
            return http_response(500, json_error("no get frames callback"));
        }
        std::string frames_json = get_frames_cb_();
        return http_response(200, frames_json);
    }

    // GET /admin/audio (retrieve buffered AAC audio frames as base64)
    if (method == "GET" && path == "/admin/audio") {
        if (!get_audio_cb_) {
            return http_response(500, json_error("no get audio callback"));
        }
        std::string audio_json = get_audio_cb_();
        return http_response(200, audio_json);
    }

    // GET /admin/stats — aggregate listener + active admin stream stats
    if (method == "GET" && path == "/admin/stats") {
        if (!get_aggregate_stats_cb_) {
            return http_response(500, json_error("no aggregate stats callback"));
        }
        return http_response(200, get_aggregate_stats_cb_());
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
