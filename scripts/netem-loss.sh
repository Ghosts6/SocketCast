#!/usr/bin/env bash
# Inject packet loss/latency on the loopback interface to exercise the
# protocol's retransmission/recovery logic (Phase 2).
#
# Usage: sudo ./netem-loss.sh [loss_pct] [delay_ms]
set -euo pipefail

LOSS="${1:-5}%"
DELAY="${2:-50}ms"
IFACE="${IFACE:-lo}"

echo "Applying ${LOSS} loss / ${DELAY} delay on ${IFACE}..."
tc qdisc add dev "${IFACE}" root netem loss "${LOSS}" delay "${DELAY}"
echo "Done. Run netem-reset.sh to remove."
