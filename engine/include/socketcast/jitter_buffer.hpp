#pragma once
// Adaptive jitter buffer with sliding-window jitter estimate.
// Buffers received packets, delivering in-order with adaptive delay to smooth
// out arrival-time variance and minimize underrun (buffering/freeze) and
// overrun (drop-oldest-low-priority).

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>

namespace socketcast {

class JitterBuffer {
public:
    struct BufferedPacket {
        uint32_t sequence_number;
        uint64_t arrival_time_us;
        uint16_t payload_size;
        uint8_t priority;  // 0=low (P-frame), 1=high (I-frame)
    };

    explicit JitterBuffer(uint32_t target_depth_ms);

    // Insert a received packet with arrival timestamp. Returns true if accepted.
    // If buffer is full (overrun), drops lowest-priority oldest packet.
    bool push(uint32_t seq, uint64_t arrival_time_us, uint16_t payload_size, uint8_t priority);

    // Pop next packet if available and within deadline. Returns empty if underrun.
    std::optional<BufferedPacket> pop(uint64_t now_us);

    // Get current buffer depth in ms based on arrival times.
    uint32_t current_depth_ms() const;

    // Get estimated jitter in ms (standard deviation of inter-packet arrival times).
    uint32_t jitter_estimate_ms() const;

    // Get buffer fullness (0-100%).
    uint32_t fullness_percent() const;

private:
    static constexpr size_t kMaxBufferSize = 256;
    static constexpr size_t kJitterWindowSize = 32;

    struct JitterSample {
        uint64_t inter_arrival_time_us;
    };

    uint32_t target_depth_ms_;
    std::deque<BufferedPacket> buffer_;
    std::deque<JitterSample> jitter_window_;
    uint64_t last_arrival_time_us_{0};
    uint32_t next_expected_seq_{0};
    bool initialized_{false};

    void updateJitterEstimate(uint64_t inter_arrival_us);
};

} // namespace socketcast
