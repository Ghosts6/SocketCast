#include "socketcast/media_source.hpp"
#include "test_util.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
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

static bool run(const std::string& cmd) {
    return std::system(cmd.c_str()) == 0;
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
    require(!source.has_audio(), "raw .h264 has no container, so no audio track");

    std::cout << "test_media_source ok chunks=" << total << "\n";
    std::remove(path);

    const char* av_path = "/tmp/socketcast_test_av.mp4";
    const std::string build_cmd =
        "ffmpeg -loglevel error -y -f lavfi -i \"sine=frequency=1000:duration=1\" -f lavfi "
        "-i \"testsrc=duration=1:size=64x64:rate=10\" -c:v libx264 -preset ultrafast -c:a aac "
        "-shortest \"" +
        std::string(av_path) + "\"";
    if (!run(build_cmd)) {
        std::cout << "test_media_source SKIP audio extraction (ffmpeg unavailable)\n";
        return 0;
    }

    socketcast::MediaSource av_source(av_path, 30);
    require(av_source.open(), "open mp4 with audio track");
    require(av_source.has_audio(), "mp4 audio track detected");

    size_t audio_total = 0;
    uint64_t last_pts = 0;
    bool first = true;
    for (auto chunks = av_source.next_audio_chunks(8); !chunks.empty();
         chunks = av_source.next_audio_chunks(8)) {
        for (const auto& c : chunks) {
            require(c.payload.size() >= 7, "adts frame at least header size");
            require(c.payload[0] == 0xFF && (c.payload[1] & 0xF0) == 0xF0, "adts sync word");
            require(c.sample_rate > 0, "sample rate populated");
            require(c.channels >= 1 && c.channels <= 8, "plausible channel count");
            if (!first) {
                require(c.pts_us >= last_pts, "audio pts is non-decreasing");
            }
            last_pts = c.pts_us;
            first = false;
            ++audio_total;
        }
    }
    require(audio_total > 0, "extracted at least one audio frame");
    require(av_source.next_audio_chunks(1).empty(), "audio cursor exhausted after full drain");

    std::cout << "test_media_source audio ok frames=" << audio_total << "\n";
    std::remove(av_path);
    return 0;
}
