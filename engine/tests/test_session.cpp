#include "socketcast/session.hpp"
#include "test_util.hpp"

#include <chrono>
#include <iostream>
#include <netinet/in.h>

using namespace socketcast;

static sockaddr_in dummy_peer() {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(9);
    return a;
}

static Packet data(uint32_t seq) {
    Packet pkt;
    pkt.header.packet_type = PacketType::Data;
    pkt.header.frame_type = FrameType::Control;
    pkt.header.sequence_number = seq;
    pkt.payload = {1, 2, 3, 4};
    return pkt;
}

int main() {
    const auto t0 = std::chrono::steady_clock::now();
    Session listener(Session::Role::Listener, dummy_peer());
    Session initiator(Session::Role::Initiator, dummy_peer());
    initiator.set_dummy_send(3, 8);

    auto syn = initiator.start();
    require(syn.size() == 1, "syn");
    require(syn[0].header.flags == kFlagSyn, "syn flag");
    require(initiator.state() == SessionState::Handshaking, "initiator handshaking");

    auto synack = listener.on_packet(syn[0], t0);
    require(synack.size() == 1, "synack");
    require((synack[0].header.flags & kFlagSyn) && (synack[0].header.flags & kFlagAck),
            "synack flags");
    require(synack[0].header.stream_id == 1, "assigned stream");

    auto opened = initiator.on_packet(synack[0], t0);
    require(initiator.state() == SessionState::Active, "initiator active");
    require(!opened.empty() && opened[0].header.packet_type == PacketType::Handshake,
            "handshake ack first");
    require(opened.size() >= 2 && opened[1].header.packet_type == PacketType::Data, "data follows");

    listener.on_packet(opened[0], t0);
    require(listener.state() == SessionState::Active, "listener active");

    auto acks = listener.on_packet(opened[1], t0);
    require(!acks.empty() && acks[0].header.packet_type == PacketType::Ack, "ack data 0");
    require(listener.stats().data_received == 1, "received 1");

    initiator.on_packet(acks[0], t0);
    require(initiator.stats().acked == 1, "acked 1");

    // Gap: seq 2 before seq 1 → NACK 1 (next expected after 0).
    auto nacks = listener.on_packet(data(2), t0);
    require(!nacks.empty() && nacks[0].header.packet_type == PacketType::Nack, "nack on gap");
    require(nacks[0].header.sequence_number == 1, "nack missing 1");

    auto retrans = initiator.on_packet(nacks[0], t0);
    require(!retrans.empty(), "nack triggers send");
    require(initiator.stats().retransmits >= 1, "retransmit counted");

    // Duplicate DATA 0 → ACK, received count unchanged.
    auto rec = listener.stats().data_received;
    auto dup_ack = listener.on_packet(data(0), t0);
    require(!dup_ack.empty() && dup_ack[0].header.packet_type == PacketType::Ack, "dup ack");
    require(listener.stats().data_received == rec, "dup not counted");

    Session listener2(Session::Role::Listener, dummy_peer());
    Session sender2(Session::Role::Initiator, dummy_peer());
    sender2.set_dummy_send(1, 8);
    auto syn2 = sender2.start();
    auto synack2 = listener2.on_packet(syn2[0], t0);
    auto open2 = sender2.on_packet(synack2[0], t0);
    require(open2.size() >= 2, "one data in window");
    auto tick = sender2.on_tick(t0 + std::chrono::milliseconds(250));
    require(!tick.empty(), "rto retransmit");
    require(sender2.stats().retransmits >= 1, "rto retransmit counted");

    std::cout << "test_session ok\n";
    return 0;
}
