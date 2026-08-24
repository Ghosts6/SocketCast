#pragma once
// Packet format — see Doc/dev/02-protocol-spec.md, Section 2 & 3.
//
// TODO(Phase 0/1): finalize on-wire layout, add serialize()/deserialize(),
// and a checksum implementation. Keep this struct POD-friendly so it maps
// cleanly onto raw bytes off the socket.

#include <cstdint>
#include <cstddef>

namespace socketcast {

enum class PacketType : uint8_t {
    Data = 0,
    Ack,
    Nack,
    ReceiverReport,
    Keepalive,
    Handshake,
};

enum class FrameType : uint8_t {
    Keyframe = 0,   // high priority
    PFrame,         // medium priority
    Audio,          // high priority (latency-sensitive)
    Control,        // protocol-internal, not media
};

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t sequence_number{};
    uint64_t timestamp{};
    PacketType packet_type{PacketType::Data};
    FrameType frame_type{FrameType::PFrame};
    uint32_t stream_id{};
    uint16_t payload_length{};
    uint32_t checksum{};
};
#pragma pack(pop)

class Packet {
public:
    Packet() = default;

    // TODO: implement wire (de)serialization.
    static Packet deserialize(const uint8_t* data, size_t len);
    size_t serialize(uint8_t* out, size_t out_capacity) const;

    PacketHeader header{};
    // TODO: payload storage (owned buffer vs. view — decide once the
    // send/receive path is designed).
};

} // namespace socketcast
