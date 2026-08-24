#!/usr/bin/env bash
# Removes any netem qdisc applied by netem-loss.sh.
set -euo pipefail

IFACE="${IFACE:-lo}"
tc qdisc del dev "${IFACE}" root netem || echo "No netem qdisc present on ${IFACE}."
