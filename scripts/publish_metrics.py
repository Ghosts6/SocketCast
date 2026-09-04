#!/usr/bin/env python3
"""
Metrics publisher sidecar for Phase 5a — wraps socketcast_client,
parses its stdout stats, and POSTs them to the control-plane API.

Usage:
  python3 scripts/publish_metrics.py --session-id <id> \
    [--control-plane-url http://localhost:8000] -- 127.0.0.1 5000

Launches socketcast_client SERVER PORT and reads its "RX: ... kbps" line
once/sec, extracting packets/bytes/bitrate and POSTing to
/api/metrics/{session_id}. Exits when the client process terminates.

Fields reported honestly (client can compute):
  - packets_received, bytes_received, bitrate_mbps

Fields reported as 0 (client does not compute in headless build):
  - frames_received (headless client never increments it)
  - rtt_ms, jitter_ms, loss_percent (no RTT/jitter tracking client-side)
"""

import argparse
import json
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


def parse_stats_line(line: str) -> dict | None:
    """Parse 'RX: {pkts} pkts, {bytes} bytes, {kbps} kbps' into metrics dict."""
    match = re.search(r"RX: (\d+) pkts, (\d+) bytes, ([\d.]+) kbps", line)
    if not match:
        return None
    pkts, bytes_recv, kbps = match.groups()
    return {
        "packets_received": int(pkts),
        "bytes_received": int(bytes_recv),
        "bitrate_mbps": round(float(kbps) / 1000, 2),
        # Honest zeros (client does not compute these in headless build):
        "frames_received": 0,  # headless client never increments frame count
        "rtt_ms": 0.0,  # no RTT tracking client-side
        "jitter_ms": 0.0,  # no jitter tracking client-side
        "loss_percent": 0.0,  # no loss tracking client-side
    }


def post_metrics(
    session_id: str, metrics: dict, control_plane_url: str
) -> bool:
    """POST metrics to control-plane /api/metrics/{session_id}."""
    url = f"{control_plane_url}/api/metrics/{session_id}"
    data = json.dumps(metrics).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=2) as response:
            return 200 <= response.status < 300
    except urllib.error.URLError as e:
        print(f"POST {url} failed: {e}", file=sys.stderr)
        return False


def main():
    parser = argparse.ArgumentParser(
        description="Wrap socketcast_client and publish its metrics to control-plane."
    )
    parser.add_argument("--session-id", required=True, help="Session ID")
    parser.add_argument(
        "--control-plane-url",
        default="http://localhost:8000",
        help="Control-plane API base URL",
    )
    # Remaining args are the client command (server, port, etc.)
    args, client_args = parser.parse_known_args()

    if not client_args:
        print(
            "Usage: publish_metrics.py --session-id <id> [--control-plane-url URL] -- SERVER PORT",
            file=sys.stderr,
        )
        sys.exit(1)

    # Strip leading '--' (argparse convention for "end of options")
    if client_args and client_args[0] == "--":
        client_args = client_args[1:]

    if not client_args:
        print("Error: missing SERVER PORT after --", file=sys.stderr)
        sys.exit(1)

    # Find the client binary (built in client/build/)
    client_bin = Path(__file__).parent.parent / "client" / "build" / "socketcast_client"
    if not client_bin.exists():
        print(f"Error: client binary not found at {client_bin}", file=sys.stderr)
        print("Build it first: cmake -S client -B client/build && cmake --build client/build", file=sys.stderr)
        sys.exit(1)

    # Launch the client as a subprocess, capturing its stdout
    print(f"Starting {client_bin} {' '.join(client_args)}", file=sys.stderr)
    try:
        proc = subprocess.Popen(
            [str(client_bin)] + client_args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
    except OSError as e:
        print(f"Failed to start client: {e}", file=sys.stderr)
        sys.exit(1)

    last_post_time = 0
    post_interval = 1.0  # seconds

    try:
        for line in proc.stdout:
            line = line.rstrip("\n")
            metrics = parse_stats_line(line)
            if metrics:
                now = time.time()
                if now - last_post_time >= post_interval:
                    if post_metrics(args.session_id, metrics, args.control_plane_url):
                        print(
                            f"Posted metrics: {metrics['packets_received']} pkts, "
                            f"{metrics['bitrate_mbps']} Mbps",
                            file=sys.stderr,
                        )
                    last_post_time = now
    except KeyboardInterrupt:
        pass
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()

    print("Publisher stopped.", file=sys.stderr)


if __name__ == "__main__":
    main()
