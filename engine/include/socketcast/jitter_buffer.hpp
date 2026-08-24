#pragma once
// Adaptive jitter buffer. See Doc/dev/02-protocol-spec.md, Section 6.
// TODO(Phase 2): sliding-window jitter estimate, adaptive depth,
// underrun (freeze/buffering) and overrun (drop-oldest-low-priority) handling.

#include <cstdint>

namespace socketcast {

class JitterBuffer {
public:
    explicit JitterBuffer(uint32_t target_depth_ms) : target_depth_ms_(target_depth_ms) {}

    // TODO: push(), pop(), adaptDepth().

private:
    uint32_t target_depth_ms_;
};

} // namespace socketcast
