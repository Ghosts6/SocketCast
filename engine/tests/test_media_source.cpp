#include "socketcast/media_source.hpp"
#include "test_util.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

static bool write_test_h264(const char* path) {
    const std::vector<uint8_t> annex_b = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E, 0xAB, 0xCD,
        0x00, 0x00, 0x00, 0x01, 0x68, 0xCE, 0x3C, 0x80, 0x11, 0x22,
        0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00, 0x33, 0x44,
        0x00, 0x00, 0x00, 0x01, 0x41, 0x9A, 0x24, 0x6C, 0x55, 0x66,
    };
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(annex_b.data()),
              static_cast<std::streamsize>(annex_b.size()));
    return static_cast<bool>(out);
}

int main() {
    const char* path = "/tmp/socketcast_test.h264";
    require(write_test_h264(path), "write test h264");

    socketcast::MediaSource source(path, 30);
    require(source.open(), "open h264");
    require(!source.eof(), "not eof at start");

    size_t total = 0;
    socketcast::FrameType saw_key = socketcast::FrameType::Control;
    socketcast::FrameType saw_p = socketcast::FrameType::Control;
    while (!source.eof()) {
        const auto chunks = source.next_chunks(8);
        if (chunks.empty()) {
            break;
        }
        for (const auto& c : chunks) {
            ++total;
            if (c.frame_type == socketcast::FrameType::Keyframe) {
                saw_key = c.frame_type;
            }
            if (c.frame_type == socketcast::FrameType::PFrame) {
                saw_p = c.frame_type;
            }
            require(!c.payload.empty(), "chunk has payload");
        }
    }
    require(total >= 4, "at least four chunks");
    require(saw_key == socketcast::FrameType::Keyframe, "saw keyframe chunk");
    require(saw_p == socketcast::FrameType::PFrame, "saw pframe chunk");
    require(source.eof(), "eof after drain");

    std::cout << "test_media_source ok chunks=" << total << "\n";
    std::remove(path);
    return 0;
}
