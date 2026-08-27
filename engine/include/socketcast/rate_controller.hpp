#pragma once
// Token-bucket rate limiter + RTT-trend congestion backoff ("BBR-inspired").
// Smooths packet transmission, adapts to loss/RTT trends, and provides
// bitrate-tier guidance for higher layers.

#include <chrono>
#include <cstdint>
#include <deque>

namespace socketcast {

class RateController {
public:
    enum class BitrateDropReason {
        NoSignal,      // RTT and loss are stable
        HighLoss,      // Packet loss exceeded threshold
        RttIncreasing, // RTT trend is up (congestion)
    };

    struct BitrateSignal {
        uint32_t rate_bps;
        BitrateDropReason reason;
    };

    explicit RateController(uint32_t initial_rate_bps);

    // Refill token bucket based on elapsed time. Call this regularly (e.g., every tick).
    void onTick(std::chrono::steady_clock::time_point now);

    // Record an ACK. Updates RTT trend and may lower bitrate.
    void onAck(std::chrono::microseconds rtt);

    // Record a lost packet. May lower bitrate.
    void onLoss();

    // Get current rate in bits per second and drop reason.
    BitrateSignal currentSignal() const;

    // How many bytes can be sent right now? (Token bucket availability)
    uint32_t availableTokens() const;

    // Consume tokens for sending packet (bytes). Returns consumed count.
    uint32_t consumeTokens(uint32_t bytes);

private:
    static constexpr uint32_t kMinRateBps = 100'000;       // 100 kbps
    static constexpr uint32_t kMaxRateBps = 100'000'000;   // 100 Mbps
    static constexpr double kLossThreshold = 0.05;         // 5% loss
    static constexpr size_t kRttWindowSize = 32;
    static constexpr int64_t kRttTrendThresholdUs = 10'000; // 10ms

    uint32_t rate_bps_;
    double tokens_{0.0};
    std::chrono::steady_clock::time_point last_refill_{};
    BitrateDropReason last_drop_reason_{BitrateDropReason::NoSignal};

    // RTT trend tracking
    std::deque<uint64_t> rtt_samples_us_;
    uint64_t rtt_baseline_us_{0};
    bool rtt_initialized_{false};

    // Loss tracking
    uint64_t packets_sent_{0};
    uint64_t packets_lost_{0};

    void updateRttTrend(std::chrono::microseconds rtt);
    void adaptRate();
};

} // namespace socketcast
