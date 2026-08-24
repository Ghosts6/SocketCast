#include "socketcast/transport.hpp"
#include <iostream>

namespace socketcast {

Transport::Transport(std::string bind_address, uint16_t port)
    : bind_address_(std::move(bind_address)), port_(port) {}

void Transport::run() {
    // TODO(Phase 1): create UDP socket, set non-blocking, register with
    // epoll, implement the event loop (recvfrom -> dispatch -> send).
    running_ = true;
    std::cout << "Transport::run() not yet implemented ("
              << bind_address_ << ":" << port_ << ")\n";
}

void Transport::stop() {
    running_ = false;
}

} // namespace socketcast
