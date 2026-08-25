#include "socketcast/rto.hpp"
#include "test_util.hpp"

#include <chrono>
#include <iostream>

int main() {
    using us = std::chrono::microseconds;
    socketcast::RtoEstimator rto;
    require(rto.currentRto() == us(200'000), "default 200ms");

    rto.onRttSample(us(100'000));
    require(rto.currentRto() == us(300'000), "first sample");

    rto.onRttSample(us(100'000));
    const auto after = rto.currentRto();
    require(after >= us(50'000) && after <= us(2'000'000), "clamped range");
    require(after < us(300'000), "stable rtt shrinks rto toward srtt");

    socketcast::RtoEstimator clamp;
    clamp.onRttSample(us(10));
    require(clamp.currentRto() >= us(50'000), "min clamp");

    socketcast::RtoEstimator hi;
    hi.onRttSample(us(5'000'000));
    require(hi.currentRto() <= us(2'000'000), "max clamp");

    std::cout << "test_rto ok rto=" << after.count() << "us\n";
    return 0;
}
