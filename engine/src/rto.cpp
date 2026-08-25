#include "socketcast/rto.hpp"

#include <algorithm>
#include <cmath>

namespace socketcast {
namespace {

constexpr int64_t kDefaultRtoUs = 200'000;
constexpr int64_t kMinRtoUs = 50'000;
constexpr int64_t kMaxRtoUs = 2'000'000;

}  // namespace

void RtoEstimator::onRttSample(std::chrono::microseconds sample) {
    const double r = static_cast<double>(std::max<int64_t>(sample.count(), 1));
    if (!initialized_) {
        srtt_us_ = r;
        rttvar_us_ = r / 2.0;
        initialized_ = true;
        return;
    }
    const double err = r - srtt_us_;
    srtt_us_ += 0.125 * err;
    rttvar_us_ += 0.25 * (std::abs(err) - rttvar_us_);
}

std::chrono::microseconds RtoEstimator::currentRto() const {
    if (!initialized_) {
        return std::chrono::microseconds(kDefaultRtoUs);
    }
    int64_t rto = static_cast<int64_t>(srtt_us_ + 4.0 * rttvar_us_);
    rto = std::max(rto, kMinRtoUs);
    rto = std::min(rto, kMaxRtoUs);
    return std::chrono::microseconds(rto);
}

}  // namespace socketcast
