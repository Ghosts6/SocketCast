#pragma once
// Token-bucket rate limiter + RTT-trend congestion backoff ("BBR-inspired",
// not full BBR — see Doc/dev/02-protocol-spec.md, Section 7).
// TODO(Phase 2): token bucket, loss/RTT-trend based bitrate-tier signal.

#include <cstdint>

namespace socketcast {

class RateController {
public:
    RateController(uint32_t initial_rate_bps) : rate_bps_(initial_rate_bps) {}

    // TODO: onAck(), onLoss(), currentRateBps(), shouldDowngradeTier().

private:
    uint32_t rate_bps_;
};

} // namespace socketcast
