#include "socketcast/nal_parser.hpp"
#include "test_util.hpp"

#include <iostream>
#include <vector>

int main() {
    using namespace socketcast;

    require(classify_nal_header(0x65) == FrameType::Keyframe, "IDR -> keyframe");
    require(classify_nal_header(0x41) == FrameType::PFrame, "non-IDR -> pframe");
    require(classify_nal_header(0x67) == FrameType::Control, "SPS -> control");

    const std::vector<uint8_t> annex_b = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E,
        0x00, 0x00, 0x00, 0x01, 0x68, 0xCE, 0x3C, 0x80,
        0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00,
        0x00, 0x00, 0x01, 0x41, 0x9A, 0x24, 0x6C,
    };

    const auto units = scan_annex_b(annex_b.data(), annex_b.size());
    require(units.size() == 4, "four NAL units");
    require(units[0].nal_type == 7, "SPS type");
    require(units[2].frame_type == FrameType::Keyframe, "IDR classified");
    require(units[3].frame_type == FrameType::PFrame, "slice classified");

    const std::vector<uint8_t> big(2500, 0xAB);
    const auto chunks = chunk_payload(big.data(), big.size());
    require(chunks.size() == 3, "split into 3 chunks");
    require(chunks[0].size() == kMaxPayloadLength, "first chunk max");
    require(chunks[2].size() == 100, "last chunk remainder");

    std::cout << "test_nal_parser ok units=" << units.size() << "\n";
    return 0;
}
