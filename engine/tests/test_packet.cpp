#include "socketcast/packet.hpp"
#include "test_util.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

using socketcast::FrameType;
using socketcast::Packet;
using socketcast::PacketType;
using socketcast::kFlagSyn;
using socketcast::kHeaderSize;
using socketcast::kMaxPayloadLength;
using socketcast::kPacketMagic;
using socketcast::kProtocolVersion;

static std::vector<uint8_t> serialize_or_die(const Packet& pkt) {
    std::vector<uint8_t> wire(kHeaderSize + pkt.payload.size() + 16);
    const size_t n = pkt.serialize(wire.data(), wire.size());
    require(n > 0, "serialize");
    wire.resize(n);
    return wire;
}

int main() {
    Packet original;
    original.header.packet_type = PacketType::Data;
    original.header.stream_id = 7;
    original.header.sequence_number = 42;
    original.header.timestamp_us = 123456789ull;
    original.payload.assign({'h', 'i', 0x00, 0xFF});

    auto wire = serialize_or_die(original);
    require(wire.size() == kHeaderSize + original.payload.size(), "serialize size");
    require(wire[0] == 'S' && wire[1] == 'C' && wire[2] == 'S' && wire[3] == 'T', "magic ASCII");

    auto parsed = Packet::deserialize(wire.data(), wire.size());
    require(parsed.has_value(), "roundtrip parse");
    require(parsed->header.magic == kPacketMagic, "magic");
    require(parsed->header.version == kProtocolVersion, "version");
    require(parsed->header.stream_id == 7, "stream_id");
    require(parsed->header.sequence_number == 42, "seq");
    require(parsed->header.timestamp_us == 123456789ull, "ts");
    require(parsed->payload == original.payload, "payload");

    require(!Packet::deserialize(wire.data(), wire.size() - 1).has_value(), "truncated");
    auto extra = wire;
    extra.push_back(0);
    require(!Packet::deserialize(extra.data(), extra.size()).has_value(), "trailing extra");

    Packet handshake;
    handshake.header.packet_type = PacketType::Handshake;
    handshake.header.frame_type = FrameType::Control;
    handshake.header.flags = kFlagSyn;
    auto hs = serialize_or_die(handshake);
    auto hs_parsed = Packet::deserialize(hs.data(), hs.size());
    require(hs_parsed.has_value(), "handshake parse");
    require(hs_parsed->header.packet_type == PacketType::Handshake, "handshake type");
    require(hs_parsed->header.flags == kFlagSyn, "syn flag");
    require(hs_parsed->payload.empty(), "handshake empty payload");

    Packet ack;
    ack.header.packet_type = PacketType::Ack;
    ack.header.sequence_number = 9;
    auto ack_wire = serialize_or_die(ack);
    auto ack_parsed = Packet::deserialize(ack_wire.data(), ack_wire.size());
    require(ack_parsed.has_value() && ack_parsed->header.sequence_number == 9, "ack seq");

    Packet maxp;
    maxp.payload.assign(kMaxPayloadLength, 0x3C);
    auto max_wire = serialize_or_die(maxp);
    require(Packet::deserialize(max_wire.data(), max_wire.size()).has_value(), "max payload");

    wire[0] ^= 0xFF;
    require(!Packet::deserialize(wire.data(), wire.size()).has_value(), "bad magic");
    wire[0] ^= 0xFF;

    wire[4] = 99;
    require(!Packet::deserialize(wire.data(), wire.size()).has_value(), "bad version");
    wire[4] = kProtocolVersion;

    wire[5] = 99;
    require(!Packet::deserialize(wire.data(), wire.size()).has_value(), "bad type");
    wire[5] = static_cast<uint8_t>(PacketType::Data);

    wire[26] = 1;
    require(!Packet::deserialize(wire.data(), wire.size()).has_value(), "nonzero reserved");
    wire[26] = 0;

    wire[30] ^= 0x01;
    require(!Packet::deserialize(wire.data(), wire.size()).has_value(), "bad crc");

    std::vector<uint8_t> tiny(4, 0);
    require(!Packet::deserialize(tiny.data(), tiny.size()).has_value(), "short header");
    require(!Packet::deserialize(nullptr, 0).has_value(), "null");

    Packet too_big;
    too_big.payload.resize(kMaxPayloadLength + 1);
    std::vector<uint8_t> small(8);
    require(too_big.serialize(small.data(), small.size()) == 0, "oversize payload");

    std::cout << "test_packet ok\n";
    return 0;
}
