#pragma once
// UDP + epoll event loop.

#include "socketcast/session.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace socketcast {

class Transport {
public:
    enum class Mode { Listen, Send };

    using RxCallback = std::function<void(const Packet& incoming, const Session::Stats& stats)>;

    Transport(std::string bind_address, uint16_t port);
    ~Transport();

    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    uint16_t bound_port() const { return bound_port_; }

    void configure_send(const std::string& peer_host, uint16_t peer_port, uint32_t count,
                        uint16_t payload_size);
    void configure_stream(const std::string& peer_host, uint16_t peer_port,
                          const std::string& input_path, uint32_t fps = 30);
    void configure_receive(const std::string& output_path);
    void set_rx_callback(RxCallback cb) { rx_callback_ = std::move(cb); }

    const Session::Stats* stats() const;
    bool send_failed() const;
    bool send_complete() const;

    void run();
    void stop();

private:
    void send_packets(const std::vector<Packet>& packets, const sockaddr_in& peer);
    void on_readable();

    std::string bind_address_;
    uint16_t port_{0};
    uint16_t bound_port_{0};
    int sock_{-1};
    int epoll_fd_{-1};
    int wakeup_fd_{-1};
    std::atomic<bool> running_{false};
    Mode mode_{Mode::Listen};
    std::unique_ptr<Session> session_;
    RxCallback rx_callback_;
};

}  // namespace socketcast
