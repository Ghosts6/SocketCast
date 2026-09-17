#include "socketcast_client/client.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#ifdef SOCKETCAST_SDL2
#include <SDL2/SDL.h>
#endif

namespace socketcast {

struct PlaybackClient::SDLContext {
#ifdef SOCKETCAST_SDL2
    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    SDL_Texture* texture{nullptr};
#endif
    bool initialized{false};
};

PlaybackClient::PlaybackClient(const std::string& server_addr, uint16_t server_port)
    : server_addr_(server_addr), server_port_(server_port), sdl_(std::make_unique<SDLContext>()) {}

PlaybackClient::~PlaybackClient() {
    if (is_connected()) {
        disconnect();
    }
    cleanup_sdl();
}

bool PlaybackClient::connect() {
    try {
        // Bind to local address for receiving. Transport handles both listen and send.
        // Uses a dynamic ephemeral port.
        transport_ = std::make_unique<Transport>("0.0.0.0", 0);
        state_ = ClientState::Connecting;

        // Configure to connect to server
        transport_->configure_send(server_addr_, server_port_, 0, 0);

        // Set up receive callback for frames
        transport_->set_rx_callback([this](const Packet& pkt, const Session::Stats& stats) {
            on_packet(pkt);
        });

        state_ = ClientState::Streaming;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Connection failed: " << e.what() << "\n";
        state_ = ClientState::Closed;
        return false;
    }
}

bool PlaybackClient::disconnect() {
    if (transport_) {
        transport_.reset();
    }
    state_ = ClientState::Closed;
    return true;
}

bool PlaybackClient::is_connected() const {
    return state_ == ClientState::Streaming;
}

bool PlaybackClient::init_sdl() {
#ifdef SOCKETCAST_SDL2
    if (sdl_->initialized) {
        return true;
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return false;
    }

    sdl_->window = SDL_CreateWindow("SocketCast Player", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, display_width_, display_height_,
                                     SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!sdl_->window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return false;
    }

    sdl_->renderer = SDL_CreateRenderer(sdl_->window, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl_->renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(sdl_->window);
        SDL_Quit();
        return false;
    }

    // Create a texture for YUV420 H.264 frames (placeholder for now)
    sdl_->texture = SDL_CreateTexture(sdl_->renderer, SDL_PIXELFORMAT_IYUV,
                                       SDL_TEXTUREACCESS_STREAMING, display_width_, display_height_);
    if (!sdl_->texture) {
        std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << "\n";
        SDL_DestroyRenderer(sdl_->renderer);
        SDL_DestroyWindow(sdl_->window);
        SDL_Quit();
        return false;
    }

    sdl_->initialized = true;
    return true;
#else
    std::cout << "SDL2 not available; running in headless mode (statistics only)\n";
    sdl_->initialized = true;  // Mark as initialized even without SDL
    return true;
#endif
}

void PlaybackClient::cleanup_sdl() {
#ifdef SOCKETCAST_SDL2
    if (sdl_->texture) {
        SDL_DestroyTexture(sdl_->texture);
    }
    if (sdl_->renderer) {
        SDL_DestroyRenderer(sdl_->renderer);
    }
    if (sdl_->window) {
        SDL_DestroyWindow(sdl_->window);
    }
    SDL_Quit();
#endif
    sdl_->initialized = false;
}

void PlaybackClient::on_packet(const Packet& pkt) {
    if (pkt.header.packet_type != PacketType::Data) {
        return;
    }

    ++stats_.packets_received;
    stats_.bytes_received += pkt.payload.size();
    last_frame_received_us_ =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();

    // Accumulate frame data (simple concatenation for now)
    incoming_frame_.insert(incoming_frame_.end(), pkt.payload.begin(), pkt.payload.end());

    // TODO: NAL-aware frame boundary detection
    // For now, treat each packet as part of the frame
}

void PlaybackClient::render_frame(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return;
    }

#ifdef SOCKETCAST_SDL2
    if (!sdl_->initialized || !sdl_->texture) {
        return;
    }

    // TODO: Decode H.264 -> YUV420, then UpdateTexture
    // For now, just update renderer with dummy data
    SDL_SetRenderDrawColor(sdl_->renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(sdl_->renderer);
    SDL_RenderPresent(sdl_->renderer);
#else
    // Headless: just log
    std::cout << "Frame received: " << data.size() << " bytes\n";
#endif
}

void PlaybackClient::update_stats() {
    // Compute bitrate (simplified)
    static auto last_update = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_update);

    if (elapsed.count() >= 1) {
        if (stats_.bytes_received > 0) {
            stats_.current_bitrate_kbps = (stats_.bytes_received * 8) / 1000 / elapsed.count();
        }
        last_update = now;
    }
}

int PlaybackClient::run() {
    if (!init_sdl()) {
        std::cerr << "Failed to initialize SDL2\n";
        return 1;
    }

    std::cout << "SocketCast native playback client\n"
              << "  Server: " << server_addr_ << ":" << server_port_ << "\n"
              << "  Display: " << display_width_ << "x" << display_height_ << "\n"
              << "  (Ctrl+C to stop)\n";

    if (!connect()) {
        return 1;
    }

    // Main event loop
    bool running = true;
    auto last_stats_print = std::chrono::steady_clock::now();

#ifdef SOCKETCAST_SDL2
    SDL_Event event;
    while (running && is_connected()) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT || event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
        }

        // incoming_frame_ is filled by the real rx callback (see connect());
        // render_frame() itself doesn't decode H.264 yet, see its own TODO.
        if (!incoming_frame_.empty()) {
            render_frame(incoming_frame_);
            incoming_frame_.clear();
            ++stats_.frames_received;
        }

        update_stats();

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_print).count() >= 1) {
            std::cout << "RX: " << stats_.packets_received << " pkts, " << stats_.bytes_received
                      << " bytes, " << stats_.current_bitrate_kbps << " kbps\n";
            last_stats_print = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(33));  // ~30 FPS
    }
#else
    while (running && is_connected()) {
        // Count frames in headless build too
        // Simulate frame processing by counting chunks as frames
        static auto last_frame_check = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (!incoming_frame_.empty() &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame_check).count() >= 33) {
            // Treat accumulated data as a frame every ~30ms (~30 FPS)
            incoming_frame_.clear();
            ++stats_.frames_received;
            last_frame_check = now;
        }

        update_stats();

        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_print).count() >= 1) {
            std::cout << "RX: " << stats_.packets_received << " pkts, " << stats_.bytes_received
                      << " bytes, " << stats_.current_bitrate_kbps << " kbps\n";
            last_stats_print = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#endif

    disconnect();
    return 0;
}

}  // namespace socketcast
