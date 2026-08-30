#!/bin/bash
# Phase 4: end-to-end demo — server streams, native client receives and plays.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="${ROOT}/engine/build/socketcast_engine"
CLIENT="${ROOT}/client/build/socketcast_client"
INPUT="${1:-}"
PORT="${2:-5000}"

if [[ -z "$INPUT" ]]; then
    echo "usage: $0 INPUT.h264 [PORT]"
    echo "  streams INPUT video through SocketCast engine"
    echo "  native client connects and displays statistics"
    exit 2
fi

if [[ ! -f "$INPUT" ]]; then
    echo "Error: input file not found: $INPUT"
    exit 1
fi

if [[ ! -x "$ENGINE" ]] || [[ ! -x "$CLIENT" ]]; then
    echo "Building SocketCast..."
    cmake -S "${ROOT}/engine" -B "${ROOT}/engine/build" -DSOCKETCAST_BUILD_TESTS=ON
    cmake --build "${ROOT}/engine/build"
    cmake -S "${ROOT}/client" -B "${ROOT}/client/build"
    cmake --build "${ROOT}/client/build"
fi

echo "=== Phase 4 End-to-End Demo ==="
echo "Starting server (streaming $INPUT)..."
"$ENGINE" stream --host 127.0.0.1 --port "$PORT" --input "$INPUT" &
SERVER_PID=$!
cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT
sleep 0.5

echo "Starting client (receiving on 127.0.0.1:$PORT)..."
"$CLIENT" 127.0.0.1 "$PORT" &
CLIENT_PID=$!
sleep 0.5

echo ""
echo "Streaming in progress... (Ctrl+C to stop)"
wait "$CLIENT_PID" 2>/dev/null || true

echo ""
echo "Phase 4 demo complete"
