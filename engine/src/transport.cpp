#include "socketcast/transport.hpp"

#include "socketcast/media_sink.hpp"
#include "socketcast/media_source.hpp"
#include "socketcast/nal_parser.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sstream>
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

// Simple base64 encoding
std::string base64_encode(const std::vector<uint8_t>& data) {
    static const char* base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int val = 0;
    int valb = 0;
    for (uint8_t c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 6) {
            valb -= 6;
            result.push_back(base64_chars[(val >> valb) & 0x3F]);
        }
    }
    if (valb > 0) {
        result.push_back(base64_chars[(val << (6 - valb)) & 0x3F]);
    }
    while (result.size() % 4) {
        result.push_back('=');
    }
    return result;
}

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

bool starts_with_annex_b(const std::vector<uint8_t>& payload) {
    if (payload.size() >= 4 && payload[0] == 0x00 && payload[1] == 0x00 &&
        payload[2] == 0x00 && payload[3] == 0x01) {
        return true;
    }
    if (payload.size() >= 3 && payload[0] == 0x00 && payload[1] == 0x00 &&
        payload[2] == 0x01) {
        return true;
    }
    return false;
}

size_t annex_b_header_len(const std::vector<uint8_t>& payload) {
    if (payload.size() >= 4 && payload[0] == 0x00 && payload[1] == 0x00 &&
        payload[2] == 0x00 && payload[3] == 0x01) {
        return 4;
    }
    if (payload.size() >= 3 && payload[0] == 0x00 && payload[1] == 0x00 &&
        payload[2] == 0x01) {
        return 3;
    }
    return 0;
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

        // Deliver copies to admin-started initiator sessions (handshake/acks)
        {
            std::lock_guard<std::mutex> lock(active_streams_mutex_);
            for (auto& [stream_id, stream] : active_streams_) {
                auto more = stream->on_packet(*parsed, now);
                send_packets(more, stream->peer());
            }
        }

        // Capture complete Annex B NALs (flush when a new start code begins)
        if (parsed->header.packet_type == PacketType::Data && !parsed->payload.empty()) {
            const uint64_t ts_us = parsed->header.timestamp_us != 0
                                       ? parsed->header.timestamp_us
                                       : static_cast<uint64_t>(
                                             std::chrono::duration_cast<std::chrono::microseconds>(
                                                 now.time_since_epoch())
                                                 .count());

            if (starts_with_annex_b(parsed->payload) && !frame_accumulator_.empty()) {
                frame_buffer_->push_frame(frame_accumulator_, last_frame_timestamp_us_,
                                          last_frame_type_ == FrameType::Keyframe);
                frame_accumulator_.clear();
            }

            last_frame_type_ = parsed->header.frame_type;
            last_frame_timestamp_us_ = ts_us;
            frame_accumulator_.insert(frame_accumulator_.end(), parsed->payload.begin(),
                                      parsed->payload.end());

            // Classify keyframe from NAL header when present on this chunk
            if (starts_with_annex_b(parsed->payload)) {
                const size_t hdr = annex_b_header_len(parsed->payload);
                if (hdr < parsed->payload.size()) {
                    last_frame_type_ = classify_nal_header(parsed->payload[hdr]);
                }
            }
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

    // Start any admin-created streams already queued before run()
    start_pending_streams();

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
                start_pending_streams();
            } else if (events[i].data.fd == sock_) {
                on_readable();
            }
        }
        // Also drain pending starts on timeout ticks (eventfd may coalesce)
        start_pending_streams();

        if (session_) {
            send_packets(session_->on_tick(std::chrono::steady_clock::now()), session_->peer());
        }

        // Tick all active streams
        {
            std::lock_guard<std::mutex> lock(active_streams_mutex_);
            std::vector<std::string> to_remove;
            const auto now = std::chrono::steady_clock::now();
            for (auto& [stream_id, stream] : active_streams_) {
                send_packets(stream->on_tick(now), stream->peer());
                if (stream->is_complete() || stream->failed()) {
                    to_remove.push_back(stream_id);
                }
            }
            for (const auto& stream_id : to_remove) {
                active_streams_.erase(stream_id);
            }
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
    admin_server_->set_get_frames_callback([this]() {
        return this->on_get_frames();
    });
    return admin_server_->start();
}

void Transport::wakeup_loop() {
    if (wakeup_fd_ < 0) {
        return;
    }
    uint64_t one = 1;
    if (::write(wakeup_fd_, &one, sizeof(one)) < 0) {
        // best-effort
    }
}

void Transport::start_pending_streams() {
    std::vector<std::pair<Session*, sockaddr_in>> to_start;
    {
        std::lock_guard<std::mutex> lock(active_streams_mutex_);
        if (pending_stream_starts_.empty()) {
            return;
        }
        for (const auto& stream_id : pending_stream_starts_) {
            auto it = active_streams_.find(stream_id);
            if (it != active_streams_.end()) {
                to_start.emplace_back(it->second.get(), it->second->peer());
            }
        }
        pending_stream_starts_.clear();
    }
    for (auto& [session, peer] : to_start) {
        send_packets(session->start(), peer);
    }
}

std::string Transport::on_start_stream(const StreamRequest& req) {
    static uint32_t stream_counter = 0;
    std::string stream_id = "stream_" + std::to_string(++stream_counter);

    try {
        auto stream_session = std::make_unique<Session>(Session::Role::Initiator,
                                                         make_addr(req.host, req.port));
        auto source = std::make_unique<MediaSource>(req.input, req.fps);
        if (!source->open()) {
            return "";
        }
        stream_session->set_media_send(std::move(source));

        {
            std::lock_guard<std::mutex> lock(active_streams_mutex_);
            active_streams_[stream_id] = std::move(stream_session);
            pending_stream_starts_.push_back(stream_id);
        }
        wakeup_loop();
        return stream_id;
    } catch (...) {
        return "";
    }
}

bool Transport::on_stop_stream(const std::string& stream_id) {
    std::lock_guard<std::mutex> lock(active_streams_mutex_);
    auto it = active_streams_.find(stream_id);
    if (it == active_streams_.end()) {
        return false;
    }
    active_streams_.erase(it);
    return true;
}

bool Transport::on_get_stream_stats(const std::string& stream_id, StreamStats& stats) {
    std::lock_guard<std::mutex> lock(active_streams_mutex_);
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

std::string Transport::on_get_frames() {
    auto frames = frame_buffer_->get_all_frames();
    std::ostringstream oss;
    oss << R"({"frames":[)";
    bool first = true;
    for (const auto& frame : frames) {
        if (!first) oss << ",";
        first = false;
        std::string b64 = base64_encode(frame->data);
        oss << R"({"timestamp_us":)" << frame->timestamp_us
            << R"(,"is_keyframe":)" << (frame->is_keyframe ? "true" : "false")
            << R"(,"data_base64":")" << b64 << R"("})";
    }
    oss << R"(]})";
    return oss.str();
}

}  // namespace socketcast
