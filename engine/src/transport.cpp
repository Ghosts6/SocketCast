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
    : bind_address_(std::move(bind_address)),
      port_(port),
      frame_buffer_(std::make_unique<FrameBuffer>()),
      audio_buffer_(std::make_unique<AudioBuffer>()) {
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

        // Deliver to admin initiator sessions with demux:
        // - SYN-ACK only to Handshaking sessions (don't let a finished stream steal it)
        // - ACK/NACK/Handshake only when stream_id matches
        // - Never deliver Data to initiators (same-socket loopback would echo our own
        //   media and corrupt initiator receive state / handshake).
        bool is_admin_stream_echo = false;
        {
            std::lock_guard<std::mutex> lock(active_streams_mutex_);
            for (auto& [stream_id, stream] : active_streams_) {
                const auto st = stream->state();
                const uint32_t sid = stream->stream_id();
                const auto ptype = parsed->header.packet_type;
                const uint8_t flags = parsed->header.flags;

                if (sid != 0 && parsed->header.stream_id == sid) {
                    is_admin_stream_echo = true;
                }

                bool deliver = false;
                if (ptype == PacketType::Handshake && (flags & kFlagSyn) && (flags & kFlagAck) &&
                    !(flags & kFlagFin)) {
                    deliver = (st == SessionState::Handshaking);
                } else if (sid != 0 && parsed->header.stream_id == sid) {
                    deliver = (ptype == PacketType::Ack || ptype == PacketType::Nack ||
                               ptype == PacketType::Handshake);
                }
                if (!deliver) {
                    continue;
                }
                auto more = stream->on_packet(*parsed, now);
                send_packets(more, stream->peer());
            }
        }

        // Capture complete Annex B NALs (flush when a new start code begins).
        // Admin-initiated streams loop back to this same socket and are
        // already captured on the send side (push_captured_frame); capturing
        // them here too would duplicate every NAL under a bogus timestamp
        // (this path falls back to wall-clock time, not media pts).
        if (!is_admin_stream_echo && parsed->header.packet_type == PacketType::Data &&
            !parsed->payload.empty()) {
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
    admin_server_->set_get_audio_callback([this]() {
        return this->on_get_audio();
    });
    admin_server_->set_get_aggregate_stats_callback([this]() {
        return this->on_get_aggregate_stats();
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
    // Collect IDs under the lock, then re-lookup when calling start() so a
    // concurrent on_start_stream clear() cannot leave us with a dangling Session*.
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(active_streams_mutex_);
        if (pending_stream_starts_.empty()) {
            return;
        }
        ids.swap(pending_stream_starts_);
    }
    for (const auto& stream_id : ids) {
        std::vector<Packet> packets;
        sockaddr_in peer{};
        {
            std::lock_guard<std::mutex> lock(active_streams_mutex_);
            auto it = active_streams_.find(stream_id);
            if (it == active_streams_.end()) {
                continue;
            }
            peer = it->second->peer();
            packets = it->second->start();
        }
        send_packets(packets, peer);
    }
}

std::string Transport::on_start_stream(const StreamRequest& req) {
    static std::atomic<uint32_t> stream_counter{0};
    std::string stream_id = "stream_" + std::to_string(++stream_counter);
    std::lock_guard<std::mutex> start_lock(stream_start_mutex_);

    try {
        auto stream_session = std::make_unique<Session>(Session::Role::Initiator,
                                                         make_addr(req.host, req.port));
        auto source = std::make_unique<MediaSource>(req.input, req.fps);
        if (!source->open()) {
            return "";
        }

        // Audio doesn't ride the UDP wire protocol; video sends at burst speed
        // (not paced to pts_us), so wall-clock-pacing audio would truncate it.
        if (audio_buffer_) {
            (void)audio_buffer_->get_all_frames();  // drop stale audio from prior stream
            if (source->has_audio()) {
                for (auto chunks = source->next_audio_chunks(64); !chunks.empty();
                     chunks = source->next_audio_chunks(64)) {
                    for (const auto& c : chunks) {
                        audio_buffer_->push_frame(c.payload, c.pts_us, c.sample_rate, c.channels);
                    }
                }
            }
        }

        // Set up frame capture callback so frames are buffered for dashboard
        stream_session->set_frame_capture_callback(
            [this](const std::vector<uint8_t>& frame_data, uint64_t ts_us, bool is_keyframe) {
                this->push_captured_frame(frame_data, ts_us, is_keyframe);
            });

        stream_session->set_media_send(std::move(source));

        {
            std::lock_guard<std::mutex> lock(active_streams_mutex_);
            // Dashboard/demo: only one admin stream at a time. Drop finished and
            // any still-running peers so a new start always handshakes cleanly.
            active_streams_.clear();
            pending_stream_starts_.clear();
            active_streams_[stream_id] = std::move(stream_session);
            pending_stream_starts_.push_back(stream_id);
        }
        {
            std::lock_guard<std::mutex> lock(param_sets_mutex_);
            cached_sps_.clear();
            cached_pps_.clear();
            cached_keyframe_.clear();
            cached_keyframe_ts_us_ = 0;
            last_keyframe_resend_ = {};
        }
        frame_accumulator_.clear();
        if (frame_buffer_) {
            (void)frame_buffer_->get_all_frames();  // drop stale NALs from prior stream
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

    // Always prepend cached SPS/PPS so a late-joining browser can configure
    // WebCodecs mid-GOP. Also periodically re-prepend the last keyframe —
    // without it a fresh viewer sees only undecodable P-frames until the next
    // natural keyframe (~8.3s away by default), so video stays black that
    // whole time. Resent on a timer, not every poll, since it's tens of KB.
    {
        std::lock_guard<std::mutex> lock(param_sets_mutex_);
        auto emit = [&](const std::vector<uint8_t>& nal, bool key, uint64_t ts_us) {
            if (nal.empty()) {
                return;
            }
            if (!first) {
                oss << ",";
            }
            first = false;
            oss << R"({"timestamp_us":)" << ts_us << R"(,"is_keyframe":)" << (key ? "true" : "false")
                << R"(,"data_base64":")" << base64_encode(nal) << R"("})";
        };
        if (!frames.empty()) {
            emit(cached_sps_, false, 0);
            emit(cached_pps_, false, 0);

            constexpr auto kKeyframeResendInterval = std::chrono::milliseconds(500);
            const auto now = std::chrono::steady_clock::now();
            if (!cached_keyframe_.empty() &&
                now - last_keyframe_resend_ >= kKeyframeResendInterval) {
                emit(cached_keyframe_, true, cached_keyframe_ts_us_);
                last_keyframe_resend_ = now;
            }
        }
    }

    for (const auto& frame : frames) {
        if (!first) {
            oss << ",";
        }
        first = false;
        std::string b64 = base64_encode(frame->data);
        oss << R"({"timestamp_us":)" << frame->timestamp_us
            << R"(,"is_keyframe":)" << (frame->is_keyframe ? "true" : "false")
            << R"(,"data_base64":")" << b64 << R"("})";
    }
    oss << R"(]})";
    return oss.str();
}

std::string Transport::on_get_audio() {
    auto frames = audio_buffer_->get_all_frames();
    std::ostringstream oss;
    oss << R"({"audio":[)";
    bool first = true;
    for (const auto& frame : frames) {
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << R"({"pts_us":)" << frame->timestamp_us << R"(,"sample_rate":)"
            << frame->sample_rate << R"(,"channels":)" << static_cast<int>(frame->channels)
            << R"(,"data_base64":")" << base64_encode(frame->data) << R"("})";
    }
    oss << R"(]})";
    return oss.str();
}

void Transport::cache_parameter_set(const std::vector<uint8_t>& annex_b) {
    size_t off = 0;
    if (annex_b.size() >= 4 && annex_b[0] == 0 && annex_b[1] == 0 && annex_b[2] == 0 &&
        annex_b[3] == 1) {
        off = 4;
    } else if (annex_b.size() >= 3 && annex_b[0] == 0 && annex_b[1] == 0 && annex_b[2] == 1) {
        off = 3;
    }
    if (off >= annex_b.size()) {
        return;
    }
    const uint8_t nal_type = annex_b[off] & 0x1f;
    std::lock_guard<std::mutex> lock(param_sets_mutex_);
    if (nal_type == 7) {
        cached_sps_ = annex_b;
    } else if (nal_type == 8) {
        cached_pps_ = annex_b;
    }
}

void Transport::push_captured_frame(const std::vector<uint8_t>& frame_data, uint64_t ts_us,
                                    bool is_keyframe) {
    cache_parameter_set(frame_data);
    if (is_keyframe) {
        std::lock_guard<std::mutex> lock(param_sets_mutex_);
        cached_keyframe_ = frame_data;
        cached_keyframe_ts_us_ = ts_us;
    }
    frame_buffer_->push_frame(frame_data, ts_us, is_keyframe);
}

std::string Transport::on_get_aggregate_stats() {
    std::ostringstream oss;
    oss << R"({"active_streams":[)";
    bool first = true;
    uint64_t data_sent = 0;
    uint64_t data_received = 0;
    uint64_t frames = 0;
    double rtt = 0.0;
    double jitter = 0.0;
    {
        std::lock_guard<std::mutex> lock(active_streams_mutex_);
        for (const auto& [id, stream] : active_streams_) {
            const auto& st = stream->stats();
            if (!first) {
                oss << ",";
            }
            first = false;
            oss << R"({"id":")" << id << R"(","data_sent":)" << st.data_sent
                << R"(,"packets_received":)" << st.data_received << R"(,"frames_received":)"
                << st.frames_received << R"(,"rtt_ms":)" << st.rtt_ms << R"(,"jitter_ms":)"
                << st.jitter_ms << R"(,"bytes_received":)" << st.media_bytes_received
                << R"(,"state":)" << static_cast<int>(stream->state()) << "}";
            data_sent += st.data_sent;
            frames += st.frames_received;
            if (st.rtt_ms > 0) {
                rtt = st.rtt_ms;
            }
            if (st.jitter_ms > 0) {
                jitter = st.jitter_ms;
            }
        }
    }
    if (session_) {
        const auto& st = session_->stats();
        data_received = st.data_received;
        if (frames == 0) {
            frames = st.frames_received;
        }
        if (rtt == 0.0) {
            rtt = st.rtt_ms;
        }
        if (jitter == 0.0) {
            jitter = st.jitter_ms;
        }
    }
    // Rough bitrate from recent send/receive counters (bytes approx via media or packets*avg)
    const double bitrate_mbps =
        0.0;  // filled by control-plane from deltas; expose counters here
    oss << R"(],"data_sent":)" << data_sent << R"(,"packets_received":)" << data_sent
        << R"(,"frames_received":)" << frames << R"(,"rtt_ms":)" << rtt << R"(,"jitter_ms":)"
        << jitter << R"(,"bitrate_mbps":)" << bitrate_mbps << R"(,"bytes_received":)"
        << ([&]() -> uint64_t {
               uint64_t bytes = 0;
               std::lock_guard<std::mutex> lock(active_streams_mutex_);
               for (const auto& [id, stream] : active_streams_) {
                   (void)id;
                   bytes += stream->stats().media_bytes_received;
               }
               if (bytes == 0 && session_) {
                   bytes = session_->stats().media_bytes_received;
               }
               return bytes;
           })()
        << "}";
    return oss.str();
}

}  // namespace socketcast
