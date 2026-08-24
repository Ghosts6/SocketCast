#pragma once
// Dynamic RTO via Jacobson's algorithm. See Doc/dev/02-protocol-spec.md,
// Section 5.
//
//   RTT_estimate  = 0.875 * RTT_estimate + 0.125 * RTT_sample
//   RTT_deviation = 0.75  * RTT_deviation + 0.25 * |RTT_sample - RTT_estimate|
//   RTO           = RTT_estimate + 4 * RTT_deviation

#include <chrono>

namespace socketcast {

class RtoEstimator {
public:
    void onRttSample(std::chrono::microseconds sample);
    std::chrono::microseconds currentRto() const;

private:
    double rtt_estimate_us_{0.0};
    double rtt_deviation_us_{0.0};
    bool initialized_{false};
};

} // namespace socketcast
