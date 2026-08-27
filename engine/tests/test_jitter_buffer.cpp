#include "socketcast/jitter_buffer.hpp"
#include "test_util.hpp"

#include <iostream>

int main() {
    using namespace socketcast;

    // Test basic push/pop
    JitterBuffer buf(50);  // 50ms target depth
    const uint64_t t0 = 1000000;  // microseconds

    require(buf.push(0, t0, 100, 1), "push seq 0");
    require(buf.push(1, t0 + 5000, 100, 1), "push seq 1 with 5ms gap");
    require(buf.push(2, t0 + 10000, 100, 1), "push seq 2 with 10ms gap");

    // Check buffer depth estimation
    const uint32_t depth = buf.current_depth_ms();
    require(depth >= 9 && depth <= 11, "depth ~10ms");

    // Can't pop yet - still buffering (haven't reached target 50ms)
    auto pkt = buf.pop(t0 + 40000);
    require(!pkt.has_value(), "pop returns nullopt while buffering");

    // After 50ms, should pop
    pkt = buf.pop(t0 + 50000);
    require(pkt.has_value(), "pop after 50ms");
    require(pkt->sequence_number == 0, "pop returns first packet");

    // Pop next
    pkt = buf.pop(t0 + 55000);
    require(pkt.has_value(), "pop seq 1");
    require(pkt->sequence_number == 1, "seq 1");

    // Test overrun: fill buffer beyond capacity and drop low-priority
    JitterBuffer small(20);
    const uint32_t hi_prio = 1;
    const uint32_t lo_prio = 0;

    // Fill with low-priority packets near max capacity
    for (uint32_t i = 0; i < 250; ++i) {
        small.push(i, t0 + i * 1000, 100, lo_prio);
    }

    const uint32_t fullness_before = small.fullness_percent();
    require(fullness_before >= 95, "buffer nearly full");

    // Try to push high-priority - should drop a low-priority packet
    require(small.push(200, t0 + 200000, 100, hi_prio), "push hi-prio to full buffer");

    // Test jitter estimate
    JitterBuffer jitter_test(30);
    const uint64_t t_base = 5000000;

    // Steady inter-arrival of 10ms
    for (int i = 0; i < 10; ++i) {
        jitter_test.push(i, t_base + i * 10000, 100, 1);
    }

    const uint32_t jitter = jitter_test.jitter_estimate_ms();
    require(jitter <= 2, "stable arrivals have low jitter estimate");

    std::cout << "test_jitter_buffer ok (depth=" << depth << "ms, jitter=" << jitter
              << "ms)\n";
    return 0;
}
