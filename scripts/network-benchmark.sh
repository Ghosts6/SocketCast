#!/bin/bash
# Network benchmark: test selective-repeat, jitter buffer, and rate control
# under induced packet loss, latency, and jitter using tc netem.
# Requires root for tc commands.

set -e

if [ ! -f engine/build/socketcast_engine ]; then
    echo "Error: engine not built. Run: cmake --build engine/build"
    exit 1
fi

if [ "$(id -u)" != "0" ]; then
    echo "Error: This script requires root for tc netem commands"
    echo "Usage: sudo $0"
    exit 1
fi

LISTEN_ADDR="127.0.0.1"
LISTEN_PORT="5555"
SEND_HOST="127.0.0.1"
SEND_PORT="5555"

# Network conditions to test
declare -a CONDITIONS=(
    "0% loss, 0ms latency (baseline)"
    "5% loss, 0ms latency"
    "10% loss, 0ms latency"
    "0% loss, 50ms latency"
    "5% loss, 50ms latency"
    "5% loss, 50ms latency, 10ms jitter"
)

# Corresponding tc netem commands
declare -a NETEM_CMDS=(
    ""
    "loss 5%"
    "loss 10%"
    "delay 50ms"
    "delay 50ms loss 5%"
    "delay 50ms 10ms loss 5%"
)

echo "=== SocketCast Network Benchmark ==="
echo "Testing reliability, rate control, and jitter buffering"
echo ""

cleanup() {
    tc qdisc del dev lo root 2>/dev/null || true
}

trap cleanup EXIT

for i in "${!CONDITIONS[@]}"; do
    condition="${CONDITIONS[$i]}"
    netem_cmd="${NETEM_CMDS[$i]}"

    echo "Testing: $condition"

    # Apply network conditions
    if [ -n "$netem_cmd" ]; then
        tc qdisc replace dev lo root netem $netem_cmd
        echo "  Applied: tc netem $netem_cmd"
    fi

    # Run the loopback test
    echo "  Running loopback test..."
    timeout 30 bash scripts/loopback-test.sh > /tmp/netbench_test_$i.log 2>&1 || {
        ret=$?
        if [ $ret -eq 124 ]; then
            echo "  Test timed out (might indicate congestion)"
        else
            echo "  Test exited with code $ret"
        fi
    }

    # Parse results
    if [ -f /tmp/netbench_test_$i.log ]; then
        acked=$(grep -oP '(?<=acked: )\d+' /tmp/netbench_test_$i.log | tail -1)
        retrans=$(grep -oP '(?<=retransmits: )\d+' /tmp/netbench_test_$i.log | tail -1)

        if [ -n "$acked" ] && [ -n "$retrans" ]; then
            echo "  Results: acked=$acked, retransmits=$retrans"
        fi
    fi

    # Clean up tc qdisc for next test
    if [ -n "$netem_cmd" ]; then
        tc qdisc del dev lo root 2>/dev/null || true
    fi

    echo ""
done

echo "=== Benchmark Complete ==="
echo "Test logs saved in /tmp/netbench_test_*.log"
