#!/usr/bin/env bash
# Stream H.264 through SocketCast and write received Annex B to a file.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${ROOT}/engine/build/socketcast_engine"
INPUT="${1:-}"
OUT="${2:-/tmp/socketcast_received.h264}"
PORT="${3:-5000}"

if [[ -z "$INPUT" ]]; then
  echo "usage: $0 INPUT_VIDEO [OUTPUT.h264] [PORT]"
  exit 2
fi

if [[ ! -x "$BIN" ]]; then
  cmake -S "${ROOT}/engine" -B "${ROOT}/engine/build" -DSOCKETCAST_BUILD_TESTS=ON
  cmake --build "${ROOT}/engine/build"
fi

"$BIN" listen --bind 127.0.0.1 --port "$PORT" --output "$OUT" &
LISTEN_PID=$!
cleanup() { kill "$LISTEN_PID" 2>/dev/null || true; }
trap cleanup EXIT
sleep 0.3

"$BIN" stream --host 127.0.0.1 --port "$PORT" --input "$INPUT"
sleep 0.2
kill -INT "$LISTEN_PID" 2>/dev/null || true
wait "$LISTEN_PID" 2>/dev/null || true

if [[ -s "$OUT" ]]; then
  echo "OK: wrote $(wc -c < "$OUT") bytes to $OUT"
else
  echo "Failed: no output at $OUT"
  exit 1
fi
