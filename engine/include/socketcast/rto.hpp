#pragma once
// Dynamic RTO via Jacobson's algorithm.

#include <chrono>

namespace socketcast {

class RtoEstimator {
public:
    void onRttSample(std::chrono::microseconds sample);
    std::chrono::microseconds currentRto() const;

private:
    double srtt_us_{0.0};
    double rttvar_us_{0.0};
    bool initialized_{false};
};

}  // namespace socketcast
