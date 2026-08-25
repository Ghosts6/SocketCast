#include "test_util.hpp"

#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

static uint16_t free_udp_port() {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    require(fd >= 0, "socket");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    require(bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0, "bind ephemeral");
    socklen_t len = sizeof(addr);
    require(getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0, "getsockname");
    const uint16_t port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

static std::string slurp_fd(int fd) {
    std::string out;
    char buf[256];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
        out.append(buf, static_cast<size_t>(n));
    }
    return out;
}

int main(int argc, char** argv) {
    require(argc >= 2, "usage: test_cli /path/to/socketcast_engine");
    const char* engine = argv[1];
    const uint16_t port = free_udp_port();
    const std::string port_s = std::to_string(port);

    int listen_pipe[2];
    require(pipe(listen_pipe) == 0, "pipe");

    const pid_t listen_pid = fork();
    require(listen_pid >= 0, "fork listen");
    if (listen_pid == 0) {
        ::close(listen_pipe[0]);
        dup2(listen_pipe[1], STDOUT_FILENO);
        dup2(listen_pipe[1], STDERR_FILENO);
        ::close(listen_pipe[1]);
        execl(engine, engine, "listen", "--bind", "127.0.0.1", "--port", port_s.c_str(), nullptr);
        _exit(127);
    }
    ::close(listen_pipe[1]);

    // Wait until listen prints its banner (bind succeeded).
    std::string listen_out;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (listen_out.find("SocketCast listen") == std::string::npos) {
        require(std::chrono::steady_clock::now() < deadline, "listen banner timeout");
        char buf[128];
        const ssize_t n = ::read(listen_pipe[0], buf, sizeof(buf));
        if (n > 0) {
            listen_out.append(buf, static_cast<size_t>(n));
        } else {
            usleep(10'000);
        }
    }

    int send_pipe[2];
    require(pipe(send_pipe) == 0, "send pipe");
    const pid_t send_pid = fork();
    require(send_pid >= 0, "fork send");
    if (send_pid == 0) {
        ::close(listen_pipe[0]);
        ::close(send_pipe[0]);
        dup2(send_pipe[1], STDOUT_FILENO);
        dup2(send_pipe[1], STDERR_FILENO);
        ::close(send_pipe[1]);
        execl(engine, engine, "send", "--host", "127.0.0.1", "--port", port_s.c_str(), "--count",
              "20", nullptr);
        _exit(127);
    }
    ::close(send_pipe[1]);

    int send_status = 0;
    require(waitpid(send_pid, &send_status, 0) == send_pid, "wait send");
    const std::string send_out = slurp_fd(send_pipe[0]);
    ::close(send_pipe[0]);
    require(WIFEXITED(send_status) && WEXITSTATUS(send_status) == 0, "send exit 0");
    require(send_out.find("acked=20") != std::string::npos, "send acked 20");

    // Give listen a moment to print the last DATA lines, then stop it.
    usleep(50'000);
    kill(listen_pid, SIGINT);
    int listen_status = 0;
    waitpid(listen_pid, &listen_status, 0);
    listen_out += slurp_fd(listen_pipe[0]);
    ::close(listen_pipe[0]);

    require(listen_out.find("handshake syn") != std::string::npos, "listen saw handshake");
    require(listen_out.find("received seq=") != std::string::npos, "listen logged data");
    require(listen_out.find("total=20") != std::string::npos ||
                listen_out.find("received=20") != std::string::npos,
            "listen captured 20");

    std::cout << "test_cli ok port=" << port << "\n";
    return 0;
}
