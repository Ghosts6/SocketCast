#include <iostream>

// Native playback client — see Doc/dev/03-roadmap-and-scope.md, Phase 4.
// TODO: speak the SocketCast protocol directly (reuse engine's packet.hpp),
// render decoded frames via SDL2 or OpenCV.
//
// NOTE: intentionally no Dockerfile for this component — it's a GUI app
// meant to run natively against a local or remote engine instance, not
// inside a headless container.

int main() {
    std::cout << "SocketCast native client — placeholder entrypoint\n";
    return 0;
}
