#include "socketcast/jitter_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace socketcast {

JitterBuffer::JitterBuffer(uint32_t target_depth_ms)
    : target_depth_ms_(target_depth_ms), next_expected_seq_(0), initialized_(false) {}

bool JitterBuffer::push(uint32_t seq, uint64_t arrival_time_us, uint16_t payload_size,
                       uint8_t priority) {
    if (!initialized_) {
        next_expected_seq_ = seq;
        initialized_ = true;
    }

    updateJitterEstimate(arrival_time_us);
    last_arrival_time_us_ = arrival_time_us;

    // If buffer is full, drop lowest-priority oldest packet (overrun handling).
    if (buffer_.size() >= kMaxBufferSize) {
        auto min_it = std::min_element(
            buffer_.begin(), buffer_.end(),
            [](const BufferedPacket& a, const BufferedPacket& b) {
                return std::tie(a.priority, a.arrival_time_us) <
                       std::tie(b.priority, b.arrival_time_us);
            });
        if (min_it != buffer_.end() && min_it->priority < priority) {
            buffer_.erase(min_it);
        } else {
            return false;  // Can't make room
        }
    }

    buffer_.push_back({seq, arrival_time_us, payload_size, priority});
    return true;
}

std::optional<JitterBuffer::BufferedPacket> JitterBuffer::pop(uint64_t now_us) {
    if (buffer_.empty()) {
        return std::nullopt;  // Underrun
    }

    const auto& pkt = buffer_.front();

    // Check if packet is within acceptable delay from its arrival.
    // Deliver if: (now - arrival) >= target_depth or no newer packets waiting.
    const uint64_t buffered_us = now_us - pkt.arrival_time_us;
    const uint64_t target_us = target_depth_ms_ * 1000ULL;

    if (buffered_us >= target_us || buffer_.size() == 1) {
        auto result = buffer_.front();
        buffer_.pop_front();
        next_expected_seq_ = pkt.sequence_number + 1;
        return result;
    }

    return std::nullopt;  // Still buffering
}

uint32_t JitterBuffer::current_depth_ms() const {
    if (buffer_.empty() || last_arrival_time_us_ == 0) {
        return 0;
    }
    const auto oldest = buffer_.front().arrival_time_us;
    const auto newest = buffer_.back().arrival_time_us;
    return static_cast<uint32_t>((newest - oldest + 500) / 1000);  // Round to ms
}

uint32_t JitterBuffer::jitter_estimate_ms() const {
    if (jitter_window_.size() < 2) {
        return 0;
    }

    double sum = 0.0;
    double sum_sq = 0.0;
    for (const auto& sample : jitter_window_) {
        const double ms = static_cast<double>(sample.inter_arrival_time_us) / 1000.0;
        sum += ms;
        sum_sq += ms * ms;
    }

    const double mean = sum / static_cast<double>(jitter_window_.size());
    const double variance = (sum_sq / static_cast<double>(jitter_window_.size())) - mean * mean;
    const double stddev = std::sqrt(std::max(0.0, variance));

    return static_cast<uint32_t>(stddev);
}

uint32_t JitterBuffer::fullness_percent() const {
    return static_cast<uint32_t>(100 * buffer_.size() / kMaxBufferSize);
}

void JitterBuffer::updateJitterEstimate(uint64_t arrival_time_us) {
    if (last_arrival_time_us_ == 0) {
        return;  // First packet
    }

    const uint64_t inter_arrival = arrival_time_us - last_arrival_time_us_;
    jitter_window_.push_back({inter_arrival});

    if (jitter_window_.size() > kJitterWindowSize) {
        jitter_window_.pop_front();
    }
}

}  // namespace socketcast
