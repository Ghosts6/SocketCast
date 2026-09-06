#pragma once
// One 1:1 session. Handshake + selective-repeat, rate control, jitter buffering,
// and Phase 3 media streaming.

#include "socketcast/jitter_buffer.hpp"
#include "socketcast/media_sink.hpp"
#include "socketcast/media_source.hpp"
#include "socketcast/packet.hpp"
#include "socketcast/rate_controller.hpp"
#include "socketcast/rto.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <netinet/in.h>
#include <vector>

namespace socketcast {

enum class SessionState {
    Handshaking,
    Active,
    Closing,
    Closed,
};

class Session {
public:
    enum class Role { Listener, Initiator };

    struct Stats {
        uint64_t data_sent{0};
        uint64_t data_received{0};
        uint64_t acked{0};
        uint64_t retransmits{0};
        uint64_t nacks_sent{0};
        uint64_t deadline_drops{0};
        uint64_t media_bytes_received{0};
        // Phase 5b: metrics for control plane
        double rtt_ms{0.0};
        double jitter_ms{0.0};
        uint64_t frames_received{0};
    };

    Session(Role role, sockaddr_in peer);

    const sockaddr_in& peer() const { return peer_; }
    void set_peer(const sockaddr_in& peer) { peer_ = peer; }

    uint32_t stream_id() const { return stream_id_; }
    SessionState state() const { return state_; }
    const Stats& stats() const { return stats_; }
    bool failed() const { return failed_; }
    bool is_complete() const;

    void set_dummy_send(uint32_t count, uint16_t payload_size);
    void set_media_send(std::unique_ptr<MediaSource> source);
    void set_media_receive(std::unique_ptr<MediaSink> sink);

    std::vector<Packet> start();
    std::vector<Packet> on_packet(const Packet& pkt,
                                  std::chrono::steady_clock::time_point now);
    std::vector<Packet> on_tick(std::chrono::steady_clock::time_point now);

    void begin_close();

private:
    static constexpr uint32_t kSendWindow = 8;
    static constexpr int kMaxRetries = 20;
    static constexpr uint32_t kListenerStreamId = 1;
    static constexpr uint64_t kSafetyMarginUs = 50'000;

    struct InFlight {
        Packet packet;
        std::chrono::steady_clock::time_point last_sent{};
        int retries{0};
    };

    Packet make_handshake(uint8_t flags) const;
    Packet make_ack(uint32_t seq) const;
    Packet make_nack(uint32_t seq) const;
    Packet make_data(uint32_t seq, std::chrono::steady_clock::time_point now) const;
    Packet make_media_packet(const MediaChunk& chunk, uint32_t seq) const;
    std::vector<Packet> fill_window(std::chrono::steady_clock::time_point now);
    std::vector<Packet> deliver_in_order(std::chrono::steady_clock::time_point now);
    bool should_retransmit(const InFlight& slot,
                           std::chrono::steady_clock::time_point now) const;
    uint8_t jitter_priority(FrameType ft) const;
    void deliver_from_jitter(std::chrono::steady_clock::time_point now);

    Role role_;
    sockaddr_in peer_{};
    uint32_t stream_id_{0};
    SessionState state_{SessionState::Handshaking};
    bool failed_{false};
    bool fin_sent_{false};
    bool fin_received_{false};
    bool media_mode_{false};
    uint64_t stream_start_us_{0};
    bool stream_start_set_{false};

    uint32_t next_seq_{0};
    uint32_t next_expected_{0};
    uint32_t dummy_remaining_{0};
    uint16_t payload_size_{64};

    std::map<uint32_t, InFlight> in_flight_;
    std::map<uint32_t, Packet> reorder_;
    std::unique_ptr<MediaSource> media_source_;
    std::unique_ptr<MediaSink> media_sink_;
    RtoEstimator rto_;
    RateController rate_controller_{1'000'000};
    JitterBuffer jitter_buffer_{50};
    Stats stats_{};
    int handshake_retries_{0};
    std::chrono::steady_clock::time_point last_handshake_sent_{};
};

}  // namespace socketcast
