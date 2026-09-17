#pragma once
// UDP + epoll event loop.

#include "socketcast/admin_server.hpp"
#include "socketcast/audio_buffer.hpp"
#include "socketcast/frame_buffer.hpp"
#include "socketcast/session.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

    // Start admin server for control plane communication
    bool start_admin_server(uint16_t admin_port = 5001);

private:
    void send_packets(const std::vector<Packet>& packets, const sockaddr_in& peer);
    void on_readable();

    // Admin server callbacks
    std::string on_start_stream(const StreamRequest& req);
    bool on_stop_stream(const std::string& stream_id);
    bool on_get_stream_stats(const std::string& stream_id, StreamStats& stats);
    std::string on_get_frames();
    std::string on_get_audio();
    std::string on_get_aggregate_stats();

    void wakeup_loop();
    void start_pending_streams();
    void cache_parameter_set(const std::vector<uint8_t>& annex_b);
    void push_captured_frame(const std::vector<uint8_t>& frame_data, uint64_t ts_us,
                             bool is_keyframe);

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
    std::unique_ptr<AdminServer> admin_server_;
    // Admin connections now run one-per-thread; this serializes on_start_stream
    // calls against each other (never against the packet-receive hot path).
    std::mutex stream_start_mutex_;
    std::mutex active_streams_mutex_;
    std::map<std::string, std::unique_ptr<Session>> active_streams_;
    std::vector<std::string> pending_stream_starts_;
    std::unique_ptr<FrameBuffer> frame_buffer_;
    std::unique_ptr<AudioBuffer> audio_buffer_;
    std::mutex param_sets_mutex_;
    std::vector<uint8_t> cached_sps_;
    std::vector<uint8_t> cached_pps_;
    // Last keyframe, resent on a timer (not every poll — it's tens of KB)
    // so a late joiner doesn't wait a full GOP for anything decodable.
    std::vector<uint8_t> cached_keyframe_;
    uint64_t cached_keyframe_ts_us_{0};
    std::chrono::steady_clock::time_point last_keyframe_resend_{};

    // Frame accumulation: flush when a new Annex B start code begins a NAL
    std::vector<uint8_t> frame_accumulator_;
    FrameType last_frame_type_{FrameType::Keyframe};
    uint64_t last_frame_timestamp_us_{0};
};

}  // namespace socketcast
