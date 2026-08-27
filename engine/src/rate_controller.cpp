#include "socketcast/rate_controller.hpp"

#include <algorithm>
#include <cmath>

namespace socketcast {

RateController::RateController(uint32_t initial_rate_bps)
    : rate_bps_(std::clamp(initial_rate_bps, kMinRateBps, kMaxRateBps)) {
    // Start with 50ms worth of tokens (allows initial burst)
    tokens_ = static_cast<double>(rate_bps_) / 8.0 * 0.05;
}

void RateController::onTick(std::chrono::steady_clock::time_point now) {
    if (last_refill_.time_since_epoch().count() == 0) {
        last_refill_ = now;
        return;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_refill_);
    const double bytes_per_us = static_cast<double>(rate_bps_) / 8'000'000.0;
    tokens_ += bytes_per_us * static_cast<double>(elapsed.count());

    // Cap tokens at 1 second worth of data (prevent accumulation).
    const double max_tokens = static_cast<double>(rate_bps_) / 8.0;
    tokens_ = std::min(tokens_, max_tokens);

    last_refill_ = now;
}

void RateController::onAck(std::chrono::microseconds rtt) {
    ++packets_sent_;
    updateRttTrend(rtt);
    adaptRate();
}

void RateController::onLoss() {
    ++packets_lost_;
    adaptRate();
}

RateController::BitrateSignal RateController::currentSignal() const {
    return {rate_bps_, last_drop_reason_};
}

uint32_t RateController::availableTokens() const {
    return static_cast<uint32_t>(tokens_);
}

uint32_t RateController::consumeTokens(uint32_t bytes) {
    const uint32_t consumed = std::min(bytes, availableTokens());
    tokens_ -= static_cast<double>(consumed);
    return consumed;
}

void RateController::updateRttTrend(std::chrono::microseconds rtt) {
    const uint64_t rtt_us = rtt.count();

    if (!rtt_initialized_) {
        rtt_baseline_us_ = rtt_us;
        rtt_initialized_ = true;
    }

    rtt_samples_us_.push_back(rtt_us);
    if (rtt_samples_us_.size() > kRttWindowSize) {
        rtt_samples_us_.pop_front();
    }
}

void RateController::adaptRate() {
    double loss_ratio = 0.0;
    if (packets_sent_ > 0) {
        loss_ratio = static_cast<double>(packets_lost_) / static_cast<double>(packets_sent_);
    }

    // Check RTT trend
    bool rtt_increasing = false;
    if (rtt_samples_us_.size() >= 8) {
        // Compare recent RTT to baseline
        uint64_t recent_sum = 0;
        for (size_t i = std::max<size_t>(0, rtt_samples_us_.size() - 4); i < rtt_samples_us_.size();
             ++i) {
            recent_sum += rtt_samples_us_[i];
        }
        const uint64_t recent_avg = recent_sum / 4;

        uint64_t old_sum = 0;
        for (size_t i = 0; i < std::min<size_t>(4, rtt_samples_us_.size()); ++i) {
            old_sum += rtt_samples_us_[i];
        }
        const uint64_t old_avg = old_sum / 4;

        if (recent_avg > old_avg + kRttTrendThresholdUs) {
            rtt_increasing = true;
        }
    }

    const uint32_t old_rate = rate_bps_;
    last_drop_reason_ = BitrateDropReason::NoSignal;

    if (loss_ratio > kLossThreshold) {
        // Backoff by 20% on loss
        rate_bps_ = std::max(kMinRateBps, static_cast<uint32_t>(rate_bps_ * 0.8));
        last_drop_reason_ = BitrateDropReason::HighLoss;
    } else if (rtt_increasing) {
        // Backoff by 10% on rising RTT (congestion signal)
        rate_bps_ = std::max(kMinRateBps, static_cast<uint32_t>(rate_bps_ * 0.9));
        last_drop_reason_ = BitrateDropReason::RttIncreasing;
    } else if (loss_ratio < 0.01 && !rtt_increasing && old_rate < kMaxRateBps) {
        // Slow start recovery: increase by 5% when stable
        rate_bps_ = std::min(kMaxRateBps, static_cast<uint32_t>(rate_bps_ * 1.05));
    }
}

}  // namespace socketcast
