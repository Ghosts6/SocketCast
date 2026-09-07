#include "socketcast/admin_server.hpp"
#include "test_util.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::string http_exchange(uint16_t port, const std::string& request) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    require(fd >= 0, "socket");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    require(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1, "pton");
    require(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0, "connect");
    require(::send(fd, request.data(), request.size(), 0) ==
                static_cast<ssize_t>(request.size()),
            "send");
    std::string resp;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            break;
        }
        resp.append(buf, static_cast<size_t>(n));
    }
    ::close(fd);
    return resp;
}

}  // namespace

int main() {
    using namespace socketcast;

    // Prefer an ephemeral-ish fixed port for the test.
    AdminServer admin(18081);
    std::string last_input;
    std::string last_host;
    uint16_t last_port = 0;
    uint32_t last_fps = 0;

    admin.set_start_stream_callback([&](const StreamRequest& req) {
        last_input = req.input;
        last_host = req.host;
        last_port = req.port;
        last_fps = req.fps;
        return "stream_test";
    });
    admin.set_get_frames_callback([]() { return R"({"frames":[]})"; });

    require(admin.start(), "admin start");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Health
    {
        const auto resp = http_exchange(18081, "GET /admin/health HTTP/1.1\r\nHost: x\r\n\r\n");
        require(resp.find("200") != std::string::npos, "health 200");
        require(resp.find(R"("status":"ok")") != std::string::npos, "health body");
    }

    // Spaced JSON (Python json.dumps style) must parse
    {
        const std::string body =
            R"({"input": "/tmp/sc_test.h264", "host": "127.0.0.1", "port": 5000, "fps": 10})";
        const std::string req =
            "POST /admin/streams HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\n"
            "Content-Length: " +
            std::to_string(body.size()) + "\r\n\r\n" + body;
        const auto resp = http_exchange(18081, req);
        require(resp.find("200") != std::string::npos, "spaced json 200");
        require(resp.find("stream_test") != std::string::npos, "stream id");
        require(last_input == "/tmp/sc_test.h264", "input parsed");
        require(last_host == "127.0.0.1", "host parsed");
        require(last_port == 5000, "port parsed");
        require(last_fps == 10, "fps parsed");
    }

    // Compact JSON still works
    {
        const std::string body =
            R"({"input":"/tmp/a.h264","host":"10.0.0.2","port":6000,"fps":24})";
        const std::string req =
            "POST /admin/streams HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\n"
            "Content-Length: " +
            std::to_string(body.size()) + "\r\n\r\n" + body;
        const auto resp = http_exchange(18081, req);
        require(resp.find("200") != std::string::npos, "compact json 200");
        require(last_input == "/tmp/a.h264", "compact input");
        require(last_port == 6000, "compact port");
    }

    admin.stop();
    std::cout << "test_admin_server ok\n";
    return 0;
}
