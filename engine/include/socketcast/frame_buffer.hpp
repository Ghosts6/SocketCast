#pragma once
// Thread-safe ring buffer for H.264 frames.

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace socketcast {

struct Frame {
    std::vector<uint8_t> data;
    uint64_t timestamp_us{0};
    bool is_keyframe{false};
};

class FrameBuffer {
public:
    explicit FrameBuffer(size_t max_frames = 30);

    // Add a frame (copies data). Returns true if added, false if buffer full.
    bool push_frame(const std::vector<uint8_t>& data, uint64_t timestamp_us, bool is_keyframe);

    // Get the latest frame without removing it. Returns nullptr if empty.
    std::shared_ptr<Frame> peek_latest();

    // Get all frames since last call. Clears the buffer after reading.
    std::vector<std::shared_ptr<Frame>> get_all_frames();

    size_t size() const;
    size_t total_frames_added() const;

private:
    mutable std::mutex mutex_;
    std::deque<std::shared_ptr<Frame>> buffer_;
    size_t max_frames_;
    uint64_t total_frames_{0};
};

}  // namespace socketcast
