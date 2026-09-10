#!/bin/bash
# Start a video stream into the SocketCast engine (Docker: file must be under Doc/dev → /media).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VIDEO="${1:-$ROOT/Doc/dev/Rick-Astley-Never-Gonna-Give-You-Up-Official-Music-Video.mp4}"
VIDEO_PATH="/media/$(basename "$VIDEO")"

echo "SocketCast Stream Starter"
echo "================================"
echo "Host file:   $VIDEO"
echo "Engine path: $VIDEO_PATH"
echo ""

if [[ ! -f "$VIDEO" ]]; then
  echo "✗ Video not found on host: $VIDEO"
  echo "  Place the file under Doc/dev/ (mounted into the engine as /media/)."
  exit 1
fi

echo "Waiting for services..."
for _ in $(seq 1 30); do
  if curl -sf http://localhost:8000/healthz > /dev/null 2>&1 && \
     curl -sf http://localhost:5001/admin/health > /dev/null 2>&1; then
    echo "✓ Services ready"
    break
  fi
  echo -n "."
  sleep 1
done
echo ""

echo "================================"
echo "BEFORE continuing:"
echo "  1) Open http://localhost:5173"
echo "  2) Connect to 127.0.0.1 : 5000  and leave it Connected"
echo "================================"
echo "Starting stream in 3 seconds..."
sleep 3

STREAM=$(curl -s -X POST http://localhost:8000/api/admin/streams \
  -H "Content-Type: application/json" \
  -d "{\"input\":\"$VIDEO_PATH\",\"host\":\"127.0.0.1\",\"port\":5000,\"fps\":30}")

STREAM_ID=$(echo "$STREAM" | jq -r '.stream_id // empty')
if [[ -z "$STREAM_ID" || "$STREAM_ID" == "null" ]]; then
  echo "✗ Stream creation failed: $STREAM"
  exit 1
fi
echo "✓ Stream started: $STREAM_ID"

# Wait until packets actually flow (fail fast if handshake stuck)
SENT=0
for i in $(seq 1 30); do
  STATS=$(curl -s "http://localhost:5001/admin/streams/$STREAM_ID/stats" || true)
  SENT=$(echo "$STATS" | jq -r '.data_sent // 0')
  if [[ "$SENT" =~ ^[0-9]+$ ]] && (( SENT > 0 )); then
    echo "✓ Engine is sending (data_sent=$SENT) — watch the dashboard canvas now."
    echo "  Stream keeps running in the engine; this script will exit."
    echo "  Re-run the script anytime for another pass (while still Connected)."
    exit 0
  fi
  echo -ne "\r  waiting for first packets… (${i}s)   "
  sleep 1
done

echo ""
echo "✗ Stream $STREAM_ID never sent packets (data_sent=0)."
echo "  Try: docker restart socketcast-engine && re-run this script."
exit 1
