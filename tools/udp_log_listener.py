#!/usr/bin/env python3
"""Listen for ESP32 UDP debug logs."""

from __future__ import annotations

import argparse
import socket
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description="Listen for Pajoniiir ESP32 UDP logs")
    parser.add_argument("--host", default="0.0.0.0", help="local bind address")
    parser.add_argument("--port", type=int, default=3333, help="local UDP port")
    args = parser.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((args.host, args.port))
        print(f"listening for UDP logs on {args.host}:{args.port}", flush=True)
        while True:
            data, addr = sock.recvfrom(4096)
            text = data.decode("utf-8", errors="replace")
            sys.stdout.write(text)
            if text and not text.endswith("\n"):
                sys.stdout.write("\n")
            sys.stdout.flush()


if __name__ == "__main__":
    raise SystemExit(main())
