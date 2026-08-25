#include "socketcast/transport.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

static void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    using namespace socketcast;
    Transport server("127.0.0.1", 0);
    const uint16_t port = server.bound_port();
    require(port != 0, "ephemeral port");

    std::thread listener([&] { server.run(); });

    Transport client("127.0.0.1", 0);
    client.configure_send("127.0.0.1", port, 50, 64);
    client.run();

    require(client.send_complete(), "sender complete");
    require(!client.send_failed(), "sender not failed");
    const auto* s = client.stats();
    require(s != nullptr, "stats");
    require(s->acked == 50, "all 50 acked");

    server.stop();
    listener.join();

    const auto* rs = server.stats();
    require(rs != nullptr && rs->data_received == 50, "listener got 50");

    std::cout << "test_loopback ok acked=" << s->acked << " retransmits=" << s->retransmits
              << "\n";
    return 0;
}
