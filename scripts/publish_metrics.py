#!/usr/bin/env python3
"""Metrics publisher sidecar for SocketCast native client.

Wraps socketcast_client, parses stdout stats line, and POSTs metrics to
control-plane /api/metrics/{session_id} endpoint once per second.

Usage:
  python3 publish_metrics.py --session-id <id> \\
    --control-plane-url http://localhost:8000 -- \\
    127.0.0.1 5000
"""

import argparse
import re
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Optional


def parse_stats_line(line: str) -> Optional[dict]:
    """Parse client stdout stats line: 'RX: N pkts, M bytes, K.K kbps'."""
    match = re.search(r"RX:\s+(\d+)\s+pkts,\s+(\d+)\s+bytes,\s+([\d.]+)\s+kbps", line)
    if not match:
        return None

    return {
        "packets_received": int(match.group(1)),
        "bytes_received": int(match.group(2)),
        "bitrate_mbps": float(match.group(3)) / 1000.0,
    }


def post_metrics(session_id: str, control_plane_url: str, metrics: dict) -> bool:
    """POST metrics to control plane. Return True on success."""
    url = f"{control_plane_url}/api/metrics/{session_id}"
    payload = urllib.parse.urlencode(metrics).encode("utf-8")
    try:
        with urllib.request.urlopen(url, data=payload, timeout=2) as resp:
            return resp.status == 200
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError):
        return False


def main():
    parser = argparse.ArgumentParser(
        description="Wrap socketcast_client and publish metrics to control plane"
    )
    parser.add_argument("--session-id", required=True, help="Session ID")
    parser.add_argument(
        "--control-plane-url",
        default="http://localhost:8000",
        help="Control plane base URL (default: http://localhost:8000)",
    )
    parser.add_argument(
        "client_args",
        nargs=argparse.REMAINDER,
        help="Arguments to pass to socketcast_client (use -- to separate)",
    )

    args = parser.parse_args()

    # Remove leading '--' if present
    client_args = args.client_args
    if client_args and client_args[0] == "--":
        client_args = client_args[1:]

    if not client_args:
        print("Error: no client arguments provided (expected: -- <host> <port>)", file=sys.stderr)
        sys.exit(1)

    print(f"Starting socketcast_client with args: {client_args}", file=sys.stderr)
    print(f"Session ID: {args.session_id}", file=sys.stderr)
    print(f"Control plane: {args.control_plane_url}", file=sys.stderr)

    # Build socketcast_client command
    client_bin = "socketcast_client"
    if "build" in sys.argv[0]:
        client_bin = "./build/socketcast_client"

    try:
        proc = subprocess.Popen(
            [client_bin] + client_args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
    except FileNotFoundError:
        print(f"Error: {client_bin} not found", file=sys.stderr)
        sys.exit(1)

    last_post_time = time.time()

    try:
        while True:
            line = proc.stdout.readline()
            if not line:
                break

            sys.stdout.write(line)
            sys.stdout.flush()

            stats = parse_stats_line(line)
            if not stats:
                continue

            now = time.time()
            if now - last_post_time >= 1.0:
                success = post_metrics(args.session_id, args.control_plane_url, stats)
                status = "✓" if success else "✗"
                print(
                    f"[metrics] {status} posted: {stats['packets_received']} pkts, "
                    f"{stats['bytes_received']} bytes, {stats['bitrate_mbps']:.2f} mbps",
                    file=sys.stderr,
                )
                last_post_time = now

    except KeyboardInterrupt:
        print("\nShutdown requested", file=sys.stderr)
    finally:
        proc.terminate()
        proc.wait(timeout=5)
        print("Client stopped", file=sys.stderr)


if __name__ == "__main__":
    main()
