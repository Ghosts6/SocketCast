#pragma once
// H.264 Annex B NAL unit parsing and frame classification (Phase 3).

#include "socketcast/packet.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace socketcast {

struct NalUnit {
    size_t offset;       // start of NAL header byte (after start code)
    size_t length;       // bytes from NAL header through end of unit
    uint8_t nal_type;    // nal_unit_type (lower 5 bits of first byte)
    FrameType frame_type;
};

// Classify a NAL header byte (first byte after start code) into a frame type.
FrameType classify_nal_header(uint8_t nal_header);

// Scan Annex B buffer for 3- or 4-byte start codes. Returns units with offsets
// relative to data.
std::vector<NalUnit> scan_annex_b(const uint8_t* data, size_t len);

// Split a NAL (or any blob) into transport-sized chunks (max kMaxPayloadLength).
std::vector<std::vector<uint8_t>> chunk_payload(const uint8_t* data, size_t len);

// True if `payload` begins with a 3- or 4-byte Annex B start code (i.e. is the
// first chunk of a NAL unit, as opposed to a continuation fragment).
bool starts_with_annex_b(const std::vector<uint8_t>& payload);

}  // namespace socketcast
