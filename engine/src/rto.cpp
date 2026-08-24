#include "socketcast/rto.hpp"
#include <cmath>

namespace socketcast {

void RtoEstimator::onRttSample(std::chrono::microseconds sample) {
    const double sample_us = static_cast<double>(sample.count());
    if (!initialized_) {
        rtt_estimate_us_ = sample_us;
        rtt_deviation_us_ = sample_us / 2.0;
        initialized_ = true;
        return;
    }
    rtt_estimate_us_ = 0.875 * rtt_estimate_us_ + 0.125 * sample_us;
    rtt_deviation_us_ = 0.75 * rtt_deviation_us_ +
                         0.25 * std::abs(sample_us - rtt_estimate_us_);
}

std::chrono::microseconds RtoEstimator::currentRto() const {
    const double rto_us = rtt_estimate_us_ + 4.0 * rtt_deviation_us_;
    return std::chrono::microseconds(static_cast<int64_t>(rto_us));
}

} // namespace socketcast
