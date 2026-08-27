#include "socketcast/rate_controller.hpp"
#include "test_util.hpp"

#include <chrono>
#include <iostream>

int main() {
    using namespace socketcast;
    using us = std::chrono::microseconds;
    using ms = std::chrono::milliseconds;

    // Test initial state
    RateController rc(1'000'000);  // 1 Mbps
    auto sig = rc.currentSignal();
    require(sig.rate_bps == 1'000'000, "initial rate 1 Mbps");
    require(sig.reason == RateController::BitrateDropReason::NoSignal, "no drop reason");

    // Test token bucket refill
    auto now = std::chrono::steady_clock::now();
    rc.onTick(now);  // Initialize last_refill
    auto tokens_0 = rc.availableTokens();
    require(tokens_0 > 5'000, "tokens start at ~6.25KB for 1Mbps (50ms burst)");

    // Advance time by 100ms, should accumulate more tokens
    // 1Mbps = 125KB/s, so 100ms = 12.5KB
    now += ms(100);
    rc.onTick(now);
    auto tokens_1 = rc.availableTokens();
    require(tokens_1 >= tokens_0 + 10'000, "accumulated ~12.5KB after 100ms @ 1Mbps");

    // Test token consumption
    uint32_t consumed = rc.consumeTokens(100);
    require(consumed == 100, "consume 100 bytes");
    auto tokens_after = rc.availableTokens();
    require(tokens_after == tokens_1 - 100, "tokens reduced after consumption");

    // Test ACK updates RTT and increases rate when stable
    RateController rc2(1'000'000);
    now = std::chrono::steady_clock::now();
    rc2.onTick(now);

    // Record stable RTTs - should trigger slow-start recovery (5% increase)
    for (int i = 0; i < 10; ++i) {
        rc2.onAck(us(50'000));  // 50ms RTT
    }
    auto sig2 = rc2.currentSignal();
    require(sig2.rate_bps >= 1'000'000, "rate increases or stays stable with steady RTT");
    require(sig2.reason == RateController::BitrateDropReason::NoSignal, "reason is NoSignal");

    // Test RTT increase triggers backoff
    RateController rc3(1'000'000);
    for (int i = 0; i < 8; ++i) {
        rc3.onAck(us(50'000));
    }
    // Now inject higher RTT samples to trigger trend detection
    for (int i = 0; i < 4; ++i) {
        rc3.onAck(us(100'000));  // RTT jumped
    }
    auto sig3 = rc3.currentSignal();
    require(sig3.rate_bps < 1'000'000, "rate decreased on rising RTT");
    require(sig3.reason == RateController::BitrateDropReason::RttIncreasing,
            "reason is RTT increasing");

    // Test loss triggers backoff
    RateController rc4(1'000'000);
    for (int i = 0; i < 10; ++i) {
        rc4.onAck(us(50'000));
        if (i % 2 == 0) {
            rc4.onLoss();  // 50% loss
        }
    }
    auto sig4 = rc4.currentSignal();
    require(sig4.rate_bps < 1'000'000, "rate decreased on loss");
    require(sig4.reason == RateController::BitrateDropReason::HighLoss, "reason is loss");

    // Test rate clamping
    RateController rc_min(10'000);  // Requested below min
    auto sig_min = rc_min.currentSignal();
    require(sig_min.rate_bps >= 100'000, "rate clamped to minimum");

    RateController rc_max(200'000'000);  // Requested above max
    auto sig_max = rc_max.currentSignal();
    require(sig_max.rate_bps <= 100'000'000, "rate clamped to maximum");

    std::cout << "test_rate_controller ok (init=" << sig.rate_bps
              << " bps, after_rtt_trend=" << sig3.rate_bps << " bps, after_loss=" << sig4.rate_bps
              << " bps)\n";
    return 0;
}
