#pragma once
// HTTP admin server for control plane communication.
// Listens on 127.0.0.1:5001 and exposes:
// - GET /admin/health
// - POST /admin/streams (start stream)
// - DELETE /admin/streams/{id} (stop stream)
// - GET /admin/streams/{id}/stats
// - GET /admin/frames (retrieve buffered frames)

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace socketcast {

class Session;

struct StreamRequest {
    std::string input;
    std::string host;
    uint16_t port{5000};
    uint32_t fps{30};
};

struct StreamStats {
    double rtt_ms{0.0};
    double jitter_ms{0.0};
    uint64_t frames_received{0};
    uint64_t packets_received{0};
    uint64_t bytes_received{0};
    uint64_t data_sent{0};
};

class AdminServer {
public:
    AdminServer(uint16_t port = 5001);
    ~AdminServer();

    // Start the admin server in a background thread
    bool start();

    // Stop the admin server
    void stop();

    bool is_running() const { return running_.load(); }

    // Register callbacks for stream control
    using StartStreamCallback = std::function<std::string(const StreamRequest&)>;
    using StopStreamCallback = std::function<bool(const std::string&)>;
    using GetStatsCallback = std::function<bool(const std::string&, StreamStats&)>;
    using GetFramesCallback = std::function<std::string()>;

    void set_start_stream_callback(StartStreamCallback cb) { start_stream_cb_ = cb; }
    void set_stop_stream_callback(StopStreamCallback cb) { stop_stream_cb_ = cb; }
    void set_get_stats_callback(GetStatsCallback cb) { get_stats_cb_ = cb; }
    void set_get_frames_callback(GetFramesCallback cb) { get_frames_cb_ = cb; }

private:
    static constexpr int kBacklog = 5;
    static constexpr int kBufferSize = 8192;
    static constexpr int kTimeoutMs = 5000;

    uint16_t port_;
    int listen_fd_{-1};
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> server_thread_;

    StartStreamCallback start_stream_cb_;
    StopStreamCallback stop_stream_cb_;
    GetStatsCallback get_stats_cb_;
    GetFramesCallback get_frames_cb_;

    void run_server();
    void handle_connection(int client_fd);
    std::string handle_request(const std::string& method, const std::string& path,
                                const std::string& body);

    // HTTP response helpers
    std::string http_response(int status_code, const std::string& body);
    std::string json_error(const std::string& message);
};

}  // namespace socketcast
