#pragma once
// SocketCast native playback client (Phase 4).
// Speaks the protocol directly, receives H.264 frames, renders via SDL2.

#include "socketcast/packet.hpp"
#include "socketcast/session.hpp"
#include "socketcast/transport.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace socketcast {

struct PlaybackStats {
    uint64_t frames_received{0};
    uint64_t packets_received{0};
    uint64_t packets_lost{0};
    uint64_t bytes_received{0};
    uint32_t current_bitrate_kbps{0};
};

class PlaybackClient {
public:
    explicit PlaybackClient(const std::string& server_addr, uint16_t server_port);
    ~PlaybackClient();

    // Connect to server and start receiving
    bool connect();
    bool disconnect();
    bool is_connected() const;

    // Run event loop (blocking)
    int run();

    // Get statistics
    const PlaybackStats& stats() const { return stats_; }

private:
    enum class ClientState {
        Idle,
        Connecting,
        Streaming,
        Disconnecting,
        Closed,
    };

    struct FrameBuffer {
        std::vector<uint8_t> data;
        uint64_t pts_us{0};
        uint32_t frame_type{0};
    };

    std::string server_addr_;
    uint16_t server_port_;
    ClientState state_{ClientState::Idle};
    std::unique_ptr<Transport> transport_;
    PlaybackStats stats_{};

    // Frame reassembly
    std::vector<uint8_t> incoming_frame_;
    uint64_t last_frame_received_us_{0};
    uint32_t display_width_{1280};
    uint32_t display_height_{720};

    // SDL2 state (opaque pointer)
    struct SDLContext;
    std::unique_ptr<SDLContext> sdl_;

    bool init_sdl();
    void cleanup_sdl();
    void on_packet(const Packet& pkt);
    void render_frame(const std::vector<uint8_t>& data);
    void update_stats();
};

}  // namespace socketcast
