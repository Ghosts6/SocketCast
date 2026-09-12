#include "socketcast/audio_buffer.hpp"
#include "test_util.hpp"

#include <iostream>
#include <vector>

int main() {
    using socketcast::AudioBuffer;

    {
        AudioBuffer buf(4);
        require(buf.size() == 0, "starts empty");

        const std::vector<uint8_t> data = {0xFF, 0xF1, 0x4C, 0x80, 0x00, 0x1F, 0xFC};
        require(buf.push_frame(data, 0, 48000, 2), "push frame 1");
        require(buf.push_frame(data, 21333, 48000, 2), "push frame 2");
        require(buf.size() == 2, "size after two pushes");
        require(buf.total_frames_added() == 2, "total counter after two pushes");
    }

    {
        // Buffer full: oldest frame is dropped, not the push itself.
        AudioBuffer buf(2);
        const std::vector<uint8_t> a = {0x01};
        const std::vector<uint8_t> b = {0x02};
        const std::vector<uint8_t> c = {0x03};
        buf.push_frame(a, 0, 48000, 2);
        buf.push_frame(b, 1000, 48000, 2);
        buf.push_frame(c, 2000, 48000, 2);
        require(buf.size() == 2, "eviction keeps buffer at max size");
        require(buf.total_frames_added() == 3, "total counter still counts evicted frames");

        auto frames = buf.get_all_frames();
        require(frames.size() == 2, "drained two frames");
        require(frames.front()->data == b, "oldest surviving frame is b");
        require(frames.back()->data == c, "newest frame is c");
        require(frames.front()->sample_rate == 48000, "sample rate preserved");
        require(frames.front()->channels == 2, "channel count preserved");
    }

    {
        // get_all_frames clears the buffer.
        AudioBuffer buf(4);
        buf.push_frame({0x01}, 0, 44100, 1);
        auto first = buf.get_all_frames();
        require(first.size() == 1, "first drain returns the frame");
        auto second = buf.get_all_frames();
        require(second.empty(), "second drain is empty");
        require(buf.size() == 0, "size is zero after drain");
    }

    std::cout << "test_audio_buffer ok\n";
    return 0;
}
