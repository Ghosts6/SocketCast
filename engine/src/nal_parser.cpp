#include "socketcast/nal_parser.hpp"

namespace socketcast {
namespace {

bool is_start3(const uint8_t* p) {
    return p[0] == 0 && p[1] == 0 && p[2] == 1;
}

bool is_start4(const uint8_t* p) {
    return p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1;
}

}  // namespace

FrameType classify_nal_header(uint8_t nal_header) {
    const uint8_t nal_type = nal_header & 0x1F;
    switch (nal_type) {
        case 5:   // IDR slice
            return FrameType::Keyframe;
        case 1:   // non-IDR slice
            return FrameType::PFrame;
        case 6:   // SEI — often precedes keyframes; treat as control metadata
        case 7:   // SPS
        case 8:   // PPS
        case 9:   // AUD
            return FrameType::Control;
        default:
            return FrameType::PFrame;
    }
}

std::vector<NalUnit> scan_annex_b(const uint8_t* data, size_t len) {
    std::vector<NalUnit> units;
    if (data == nullptr || len < 4) {
        return units;
    }

    size_t i = 0;
    while (i + 3 < len) {
        size_t sc = 0;
        if (is_start4(data + i)) {
            sc = 4;
        } else if (is_start3(data + i)) {
            sc = 3;
        } else {
            ++i;
            continue;
        }

        const size_t nal_off = i + sc;
        if (nal_off >= len) {
            break;
        }

        size_t next = nal_off + 1;
        while (next + 3 < len) {
            if (is_start4(data + next) || is_start3(data + next)) {
                break;
            }
            ++next;
        }
        if (next + 3 >= len) {
            next = len;
        }

        const uint8_t hdr = data[nal_off];
        units.push_back({nal_off, next - nal_off, static_cast<uint8_t>(hdr & 0x1F),
                         classify_nal_header(hdr)});
        i = next;
    }
    return units;
}

std::vector<std::vector<uint8_t>> chunk_payload(const uint8_t* data, size_t len) {
    std::vector<std::vector<uint8_t>> chunks;
    if (data == nullptr || len == 0) {
        return chunks;
    }
    for (size_t off = 0; off < len; off += kMaxPayloadLength) {
        const size_t n = std::min<size_t>(kMaxPayloadLength, len - off);
        chunks.emplace_back(data + off, data + off + n);
    }
    return chunks;
}

bool starts_with_annex_b(const std::vector<uint8_t>& payload) {
    if (payload.size() >= 4 && payload[0] == 0 && payload[1] == 0 && payload[2] == 0 &&
        payload[3] == 1) {
        return true;
    }
    if (payload.size() >= 3 && payload[0] == 0 && payload[1] == 0 && payload[2] == 1) {
        return true;
    }
    return false;
}

}  // namespace socketcast
