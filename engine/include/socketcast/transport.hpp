#pragma once
// Core transport engine: owns the UDP socket, the epoll event loop,
// and dispatches packets to the right Session.
// TODO(Phase 1): epoll-based non-blocking event loop.
// TODO(Phase 8, stretch): io_uring variant, benchmarked against epoll.

#include <cstdint>
#include <string>

namespace socketcast {

class Transport {
public:
    Transport(std::string bind_address, uint16_t port);

    // Starts the event loop. Blocks until stop() is called.
    void run();
    void stop();

    // TODO: sendPacket(), registerSession(), retransmission scheduling,
    // NACK handling, receiver-report handling.

private:
    std::string bind_address_;
    uint16_t port_;
    bool running_{false};
};

} // namespace socketcast
