#include <iostream>
#include "socketcast/transport.hpp"

int main(int argc, char** argv) {
    // TODO: parse args/config (bind address, port, log level).
    std::cout << "SocketCast engine — placeholder entrypoint\n";

    socketcast::Transport transport("0.0.0.0", 5000);
    // transport.run();  // TODO: enable once Phase 1 event loop exists.

    (void)argc;
    (void)argv;
    return 0;
}
