#!/usr/bin/env python3
"""Append a board UART0 console to a log file, surviving unplug/replug.

Usage: hil_capture.py PORT LOGFILE [BAUD]
Every (re)attach writes a marker line so restarts are visible in the log.
"""
from __future__ import annotations

import sys
import time

import serial


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    port, log_path = sys.argv[1], sys.argv[2]
    baud = int(sys.argv[3]) if len(sys.argv) > 3 else 115200
    with open(log_path, "ab") as log:
        while True:
            try:
                with serial.Serial(port, baud, timeout=2) as ser:
                    stamp = time.strftime("%Y-%m-%d %H:%M:%S")
                    log.write(f"\n[hil] attached {port} @{baud} {stamp}\n".encode())
                    log.flush()
                    while True:
                        data = ser.read(4096)
                        if data:
                            log.write(data)
                            log.flush()
            except (serial.SerialException, OSError):
                time.sleep(1)


if __name__ == "__main__":
    raise SystemExit(main())
