#include "socketcast_client/client.hpp"

#include <iostream>
#include <stdexcept>

// Native playback client.
// Speaks the SocketCast protocol directly, receives H.264 frames, renders via SDL2.
//
// NOTE: intentionally no Dockerfile for this component — it's a GUI app
// meant to run natively against a local or remote engine instance, not
// inside a headless container.

void usage(const char* prog) {
    std::cerr << "usage: " << prog << " SERVER [PORT]\n"
              << "  SERVER: hostname or IP of SocketCast engine\n"
              << "  PORT:   server port (default 5000)\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2 || std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
        usage(argv[0]);
        return argc < 2 ? 1 : 0;
    }

    std::string server = argv[1];
    uint16_t port = 5000;

    if (argc >= 3) {
        try {
            port = static_cast<uint16_t>(std::stoi(argv[2]));
        } catch (const std::exception& e) {
            std::cerr << "Invalid port: " << argv[2] << "\n";
            usage(argv[0]);
            return 1;
        }
    }

    try {
        socketcast::PlaybackClient client(server, port);
        return client.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
