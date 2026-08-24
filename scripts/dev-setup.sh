#!/usr/bin/env bash
# One-shot local dev bootstrap. Adjust as the toolchain solidifies.
set -euo pipefail

echo "== SocketCast dev setup =="

echo "-- control-plane (Python) --"
( cd control-plane && python3 -m venv .venv && . .venv/bin/activate && pip install -r requirements-dev.txt )

echo "-- dashboard (Node) --"
( cd dashboard && npm install )

echo "-- engine (C++) --"
echo "Requires: cmake, ninja (or make), a C++17 compiler."
echo "  cmake -S engine -B engine/build -G Ninja"
echo "  cmake --build engine/build"

echo "Done."
