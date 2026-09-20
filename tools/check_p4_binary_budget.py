#!/usr/bin/env python3
"""Enforce and report an absolute ESP32-P4 application binary budget."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def parse_limit(value: str) -> int:
    limit = int(value, 0)
    if limit <= 0:
        raise argparse.ArgumentTypeError("limit must be positive")
    return limit


def inspect_binary(path: Path, max_bytes: int) -> dict[str, object]:
    payload = path.read_bytes()
    size = len(payload)
    return {
        "path": str(path),
        "size_bytes": size,
        "max_bytes": max_bytes,
        "remaining_bytes": max_bytes - size,
        "sha256": hashlib.sha256(payload).hexdigest(),
        "within_budget": size <= max_bytes,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--max-bytes", required=True, type=parse_limit)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    report = inspect_binary(args.binary, args.max_bytes)
    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0 if report["within_budget"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
