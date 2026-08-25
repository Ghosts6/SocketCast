#include "socketcast/packet.hpp"

#include <algorithm>
#include <array>

namespace socketcast {
namespace {

constexpr uint32_t kCrcPoly = 0xEDB88320u;

constexpr std::array<uint32_t, 256> kCrcTable = []() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j) {
            c = (c & 1u) ? (kCrcPoly ^ (c >> 1)) : (c >> 1);
        }
        table[i] = c;
    }
    return table;
}();

void put_u16_be(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}

void put_u32_be(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

void put_u64_be(uint8_t* p, uint64_t v) {
    put_u32_be(p, static_cast<uint32_t>(v >> 32));
    put_u32_be(p + 4, static_cast<uint32_t>(v));
}

uint16_t get_u16_be(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t get_u32_be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

uint64_t get_u64_be(const uint8_t* p) {
    return (static_cast<uint64_t>(get_u32_be(p)) << 32) | get_u32_be(p + 4);
}

bool packet_type_ok(uint8_t v) {
    return v <= static_cast<uint8_t>(PacketType::Handshake);
}

bool frame_type_ok(uint8_t v) {
    return v <= static_cast<uint8_t>(FrameType::Control);
}

void write_header_unchecked(uint8_t* out, const PacketHeader& h) {
    put_u32_be(out + 0, h.magic);
    out[4] = h.version;
    out[5] = static_cast<uint8_t>(h.packet_type);
    out[6] = static_cast<uint8_t>(h.frame_type);
    out[7] = h.flags;
    put_u32_be(out + 8, h.stream_id);
    put_u32_be(out + 12, h.sequence_number);
    put_u64_be(out + 16, h.timestamp_us);
    put_u16_be(out + 24, h.payload_length);
    put_u16_be(out + 26, h.reserved);
    put_u32_be(out + 28, h.checksum);
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        c = kCrcTable[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

size_t Packet::serialize(uint8_t* out, size_t out_capacity) const {
    if (payload.size() > kMaxPayloadLength) {
        return 0;
    }
    const size_t total = kHeaderSize + payload.size();
    if (out_capacity < total) {
        return 0;
    }

    PacketHeader h = header;
    h.magic = kPacketMagic;
    h.version = kProtocolVersion;
    h.payload_length = static_cast<uint16_t>(payload.size());
    h.reserved = 0;
    h.checksum = 0;

    write_header_unchecked(out, h);
    if (!payload.empty()) {
        std::copy(payload.begin(), payload.end(), out + kHeaderSize);
    }
    const uint32_t sum = crc32(out, total);
    put_u32_be(out + 28, sum);
    return total;
}

std::optional<Packet> Packet::deserialize(const uint8_t* data, size_t len) {
    if (data == nullptr || len < kHeaderSize) {
        return std::nullopt;
    }

    Packet pkt;
    pkt.header.magic = get_u32_be(data + 0);
    pkt.header.version = data[4];
    const uint8_t ptype = data[5];
    const uint8_t ftype = data[6];
    pkt.header.flags = data[7];
    pkt.header.stream_id = get_u32_be(data + 8);
    pkt.header.sequence_number = get_u32_be(data + 12);
    pkt.header.timestamp_us = get_u64_be(data + 16);
    pkt.header.payload_length = get_u16_be(data + 24);
    pkt.header.reserved = get_u16_be(data + 26);
    pkt.header.checksum = get_u32_be(data + 28);

    if (pkt.header.magic != kPacketMagic || pkt.header.version != kProtocolVersion) {
        return std::nullopt;
    }
    if (pkt.header.reserved != 0) {
        return std::nullopt;
    }
    if (!packet_type_ok(ptype) || !frame_type_ok(ftype)) {
        return std::nullopt;
    }
    pkt.header.packet_type = static_cast<PacketType>(ptype);
    pkt.header.frame_type = static_cast<FrameType>(ftype);

    if (pkt.header.payload_length > kMaxPayloadLength) {
        return std::nullopt;
    }
    if (len != kHeaderSize + pkt.header.payload_length) {
        return std::nullopt;
    }

    std::vector<uint8_t> scratch(data, data + len);
    scratch[28] = scratch[29] = scratch[30] = scratch[31] = 0;
    if (crc32(scratch.data(), scratch.size()) != pkt.header.checksum) {
        return std::nullopt;
    }

    if (pkt.header.payload_length > 0) {
        pkt.payload.assign(data + kHeaderSize, data + len);
    }
    return pkt;
}

}  // namespace socketcast
