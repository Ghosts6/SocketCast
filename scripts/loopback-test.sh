#!/usr/bin/env bash
# Two-process loopback check: listen + send dummy DATA over UDP.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${ROOT}/engine/build/socketcast_engine"
if [[ ! -x "$BIN" ]]; then
  cmake -S "${ROOT}/engine" -B "${ROOT}/engine/build" -DSOCKETCAST_BUILD_TESTS=ON
  cmake --build "${ROOT}/engine/build"
fi
PORT="${1:-5000}"
"$BIN" listen --bind 127.0.0.1 --port "$PORT" &
LISTEN_PID=$!
cleanup() { kill "$LISTEN_PID" 2>/dev/null || true; }
trap cleanup EXIT
sleep 0.2
"$BIN" send --host 127.0.0.1 --port "$PORT" --count 100 --size 64
