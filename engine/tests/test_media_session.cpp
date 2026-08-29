#include "socketcast/media_source.hpp"
#include "socketcast/session.hpp"
#include "test_util.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <netinet/in.h>

using namespace socketcast;

static sockaddr_in dummy_peer() {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(9);
    return a;
}

static bool write_h264(const char* path) {
    const std::vector<uint8_t> annex_b = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E,
        0x00, 0x00, 0x00, 0x01, 0x68, 0xCE, 0x3C, 0x80,
        0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00,
    };
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(annex_b.data()),
              static_cast<std::streamsize>(annex_b.size()));
    return static_cast<bool>(out);
}

int main() {
    const char* path = "/tmp/socketcast_deadline.h264";
    require(write_h264(path), "write h264");

    const auto t0 = std::chrono::steady_clock::now();
    Session sender(Session::Role::Initiator, dummy_peer());
    Session listener(Session::Role::Listener, dummy_peer());

    auto source = std::make_unique<MediaSource>(path, 30);
    require(source->open(), "open media");
    sender.set_media_send(std::move(source));

    auto syn = sender.start();
    auto synack = listener.on_packet(syn[0], t0);
    auto opened = sender.on_packet(synack[0], t0);
    listener.on_packet(opened[0], t0);
    require(sender.state() == SessionState::Active, "active");

    for (size_t i = 1; i < opened.size(); ++i) {
        listener.on_packet(opened[i], t0);
    }

    Packet nack0;
    nack0.header.packet_type = PacketType::Nack;
    nack0.header.frame_type = FrameType::Control;
    nack0.header.sequence_number = 0;

    const auto late = t0 + std::chrono::milliseconds(200);
    const auto before_drops = sender.stats().deadline_drops;
    const auto before_retx = sender.stats().retransmits;
    sender.on_packet(nack0, late);
    require(sender.stats().deadline_drops > before_drops ||
                sender.stats().retransmits == before_retx,
            "late packet deadline drop");

    std::cout << "test_media_session ok deadline_drops=" << sender.stats().deadline_drops
              << "\n";
    std::remove(path);
    return 0;
}
