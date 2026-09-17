#pragma once
// Thread-safe ring buffer for AAC audio frames.

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace socketcast {

struct AudioFrame {
    std::vector<uint8_t> data;     // AAC ADTS frame
    uint64_t timestamp_us{0};       // PTS in microseconds
    uint32_t sample_rate{48000};
    uint8_t channels{2};
};

class AudioBuffer {
public:
    // Whole track is drained in up front, not paced to wall clock, so this
    // needs to hold a full clip: 65536 frames is ~23 min at 21ms/frame.
    explicit AudioBuffer(size_t max_frames = 65536);

    // Add audio frame (copies data). Returns true if added, false if buffer full.
    bool push_frame(const std::vector<uint8_t>& data, uint64_t timestamp_us,
                    uint32_t sample_rate, uint8_t channels);

    // Get all frames since last call. Clears buffer after reading.
    // Prefer peek_all_frames() for /admin/audio serving — drain-on-read
    // permanently silences late joiners / reconnects (audio is only dumped once).
    std::vector<std::shared_ptr<AudioFrame>> get_all_frames();

    // Non-destructive read of every buffered frame (shared_ptrs into the buffer).
    std::vector<std::shared_ptr<AudioFrame>> peek_all_frames() const;

    size_t size() const;
    size_t total_frames_added() const;

private:
    mutable std::mutex mutex_;
    std::deque<std::shared_ptr<AudioFrame>> buffer_;
    size_t max_frames_;
    uint64_t total_frames_{0};
};

}  // namespace socketcast
