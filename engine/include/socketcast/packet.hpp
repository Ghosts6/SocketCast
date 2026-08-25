#pragma once
// On-wire packet format (v1: 32-byte big-endian header + CRC-32).

#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

namespace socketcast {

constexpr uint32_t kPacketMagic = 0x53435354;  // "SCST"
constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kHeaderSize = 32;
constexpr uint16_t kMaxPayloadLength = 1200;

constexpr uint8_t kFlagSyn = 0x01;
constexpr uint8_t kFlagAck = 0x02;
constexpr uint8_t kFlagFin = 0x04;

enum class PacketType : uint8_t {
    Data = 0,
    Ack,
    Nack,
    ReceiverReport,
    Keepalive,
    Handshake,
};

enum class FrameType : uint8_t {
    Keyframe = 0,
    PFrame,
    Audio,
    Control,
};

struct PacketHeader {
    uint32_t magic{kPacketMagic};
    uint8_t version{kProtocolVersion};
    PacketType packet_type{PacketType::Data};
    FrameType frame_type{FrameType::Control};
    uint8_t flags{0};
    uint32_t stream_id{0};
    uint32_t sequence_number{0};
    uint64_t timestamp_us{0};
    uint16_t payload_length{0};
    uint16_t reserved{0};
    uint32_t checksum{0};
};

class Packet {
public:
    Packet() = default;

    static std::optional<Packet> deserialize(const uint8_t* data, size_t len);
    // Returns bytes written, or 0 if out_capacity is too small or payload is too large.
    size_t serialize(uint8_t* out, size_t out_capacity) const;

    PacketHeader header{};
    std::vector<uint8_t> payload;
};

uint32_t crc32(const uint8_t* data, size_t len);

}  // namespace socketcast
