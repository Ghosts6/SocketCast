#include "socketcast/transport.hpp"

#include "socketcast/media_sink.hpp"
#include "socketcast/media_source.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace socketcast {
namespace {

constexpr size_t kMaxDatagram = kHeaderSize + kMaxPayloadLength;
constexpr int kEpollTimeoutMs = 10;

sockaddr_in make_addr(const std::string& host, uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        throw std::runtime_error("invalid IPv4 address: " + host);
    }
    return addr;
}

void set_nonblock(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::runtime_error("fcntl O_NONBLOCK failed");
    }
}

}  // namespace

Transport::Transport(std::string bind_address, uint16_t port)
    : bind_address_(std::move(bind_address)), port_(port), frame_buffer_(std::make_unique<FrameBuffer>()) {
    sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        throw std::runtime_error("socket() failed");
    }
    set_nonblock(sock_);

    sockaddr_in local = make_addr(bind_address_, port_);
    if (bind(sock_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
        const int err = errno;
        ::close(sock_);
        sock_ = -1;
        throw std::runtime_error(std::string("bind() failed: ") + std::strerror(err) + " (" +
                                 bind_address_ + ":" + std::to_string(port_) + ")");
    }

    sockaddr_in bound{};
    socklen_t blen = sizeof(bound);
    if (getsockname(sock_, reinterpret_cast<sockaddr*>(&bound), &blen) == 0) {
        bound_port_ = ntohs(bound.sin_port);
    }

    epoll_fd_ = epoll_create1(0);
    wakeup_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (epoll_fd_ < 0 || wakeup_fd_ < 0) {
        throw std::runtime_error("epoll/eventfd setup failed");
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = sock_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, sock_, &ev);
    ev.data.fd = wakeup_fd_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wakeup_fd_, &ev);
}

Transport::~Transport() {
    stop();
    if (wakeup_fd_ >= 0) {
        ::close(wakeup_fd_);
    }
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
    }
    if (sock_ >= 0) {
        ::close(sock_);
    }
}

void Transport::configure_send(const std::string& peer_host, uint16_t peer_port, uint32_t count,
                               uint16_t payload_size) {
    mode_ = Mode::Send;
    session_ = std::make_unique<Session>(Session::Role::Initiator, make_addr(peer_host, peer_port));
    session_->set_dummy_send(count, payload_size);
}

void Transport::configure_stream(const std::string& peer_host, uint16_t peer_port,
                                 const std::string& input_path, uint32_t fps) {
    mode_ = Mode::Send;
    session_ = std::make_unique<Session>(Session::Role::Initiator, make_addr(peer_host, peer_port));
    auto source = std::make_unique<MediaSource>(input_path, fps);
    if (!source->open()) {
        throw std::runtime_error("failed to open media input: " + input_path);
    }
    session_->set_media_send(std::move(source));
}

void Transport::configure_receive(const std::string& output_path) {
    auto sink = std::make_unique<MediaSink>(output_path);
    if (!sink->open()) {
        throw std::runtime_error("failed to open media output: " + output_path);
    }
    if (!session_) {
        sockaddr_in peer{};
        peer.sin_family = AF_INET;
        session_ = std::make_unique<Session>(Session::Role::Listener, peer);
    }
    session_->set_media_receive(std::move(sink));
}

const Session::Stats* Transport::stats() const {
    return session_ ? &session_->stats() : nullptr;
}

bool Transport::send_failed() const {
    return session_ && session_->failed();
}

bool Transport::send_complete() const {
    return session_ && session_->is_complete();
}

void Transport::send_packets(const std::vector<Packet>& packets, const sockaddr_in& peer) {
    uint8_t buf[kMaxDatagram];
    for (const auto& pkt : packets) {
        const size_t n = pkt.serialize(buf, sizeof(buf));
        if (n == 0) {
            continue;
        }
        ::sendto(sock_, buf, n, 0, reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
    }
}

void Transport::on_readable() {
    uint8_t buf[kMaxDatagram + 64];
    while (true) {
        sockaddr_in from{};
        socklen_t fromlen = sizeof(from);
        const ssize_t n = ::recvfrom(sock_, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from),
                                     &fromlen);
        if (n <= 0) {
            break;
        }
        auto parsed = Packet::deserialize(buf, static_cast<size_t>(n));
        if (!parsed) {
            continue;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!session_) {
            session_ = std::make_unique<Session>(Session::Role::Listener, from);
        } else if (mode_ == Mode::Listen) {
            session_->set_peer(from);
        }
        auto replies = session_->on_packet(*parsed, now);
        send_packets(replies, session_->peer());

        // Phase 5b: capture media frames
        if (parsed->header.packet_type == PacketType::Data &&
            parsed->header.frame_type == FrameType::Keyframe && !parsed->payload.empty()) {
            uint64_t ts_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                  now.time_since_epoch())
                                  .count();
            frame_buffer_->push_frame(parsed->payload, ts_us, true);
        }

        if (rx_callback_) {
            rx_callback_(*parsed, session_->stats());
        }
    }
}

void Transport::run() {
    running_ = true;
    if (session_ && mode_ == Mode::Send) {
        send_packets(session_->start(), session_->peer());
    }

    epoll_event events[8];
    while (running_) {
        if (mode_ == Mode::Send && session_ && (session_->is_complete() || session_->failed())) {
            break;
        }
        const int n = epoll_wait(epoll_fd_, events, 8, kEpollTimeoutMs);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == wakeup_fd_) {
                uint64_t val = 0;
                if (::read(wakeup_fd_, &val, sizeof(val)) < 0 && errno != EAGAIN) {
                    break;
                }
            } else if (events[i].data.fd == sock_) {
                on_readable();
            }
        }
        if (session_) {
            send_packets(session_->on_tick(std::chrono::steady_clock::now()), session_->peer());
        }
    }
    running_ = false;
}

void Transport::stop() {
    running_ = false;
    if (wakeup_fd_ >= 0) {
        uint64_t one = 1;
        if (::write(wakeup_fd_, &one, sizeof(one)) < 0) {
            // best-effort wake; run() also times out every 10ms
        }
    }
}

bool Transport::start_admin_server(uint16_t admin_port) {
    admin_server_ = std::make_unique<AdminServer>(admin_port);
    admin_server_->set_start_stream_callback([this](const StreamRequest& req) {
        return this->on_start_stream(req);
    });
    admin_server_->set_stop_stream_callback([this](const std::string& id) {
        return this->on_stop_stream(id);
    });
    admin_server_->set_get_stats_callback([this](const std::string& id, StreamStats& stats) {
        return this->on_get_stream_stats(id, stats);
    });
    return admin_server_->start();
}

std::string Transport::on_start_stream(const StreamRequest& req) {
    // Generate stream ID
    static uint32_t stream_counter = 0;
    std::string stream_id = "stream_" + std::to_string(++stream_counter);

    // Create a new session for this stream
    try {
        auto stream_session = std::make_unique<Session>(Session::Role::Initiator,
                                                         make_addr(req.host, req.port));
        auto source = std::make_unique<MediaSource>(req.input, req.fps);
        if (!source->open()) {
            return "";
        }
        stream_session->set_media_send(std::move(source));

        active_streams_[stream_id] = std::move(stream_session);
        return stream_id;
    } catch (...) {
        return "";
    }
}

bool Transport::on_stop_stream(const std::string& stream_id) {
    auto it = active_streams_.find(stream_id);
    if (it == active_streams_.end()) {
        return false;
    }
    active_streams_.erase(it);
    return true;
}

bool Transport::on_get_stream_stats(const std::string& stream_id, StreamStats& stats) {
    auto it = active_streams_.find(stream_id);
    if (it == active_streams_.end()) {
        return false;
    }
    const auto& session_stats = it->second->stats();
    stats.rtt_ms = session_stats.rtt_ms;
    stats.jitter_ms = session_stats.jitter_ms;
    stats.frames_received = session_stats.frames_received;
    stats.packets_received = session_stats.data_received;
    stats.bytes_received = session_stats.media_bytes_received;
    stats.data_sent = session_stats.data_sent;
    return true;
}

}  // namespace socketcast
