#!/usr/bin/env python3
"""Create a deterministic software/hardware acceptance evidence manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
LOCK_PATHS = (
    "firmware/main-deck-p4/dependencies.lock",
    "firmware/p4-dual-usb-spike/dependencies.lock",
    "firmware/p4-only-software-harness/dependencies.lock",
)
FEATURE_PATH = "firmware/main-deck-p4/sdkconfig.p4_local_controller"


def file_record(root: Path, relative: str) -> dict[str, object]:
    path = root / relative
    payload = path.read_bytes()
    return {
        "path": relative,
        "size_bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
    }


def build_manifest(root: Path, commit: str, artifact: Path | None) -> dict[str, object]:
    if not COMMIT_RE.fullmatch(commit):
        raise ValueError("commit must be a lowercase 40-character SHA-1")
    records = [file_record(root, relative) for relative in LOCK_PATHS]
    feature = (root / FEATURE_PATH).read_text(encoding="utf-8")
    if "CONFIG_PAJONIIIR_P4_LOCAL_CONTROLLER=y" not in feature.splitlines():
        raise ValueError("P4-local feature overlay is missing or disabled")

    result: dict[str, object] = {
        "schema": "pajoniiir.p4-dual-usb-acceptance.v1",
        "software_commit": commit,
        "hardware_accepted": False,
        "feature_overlay": FEATURE_PATH,
        "dependency_locks": records,
    }
    if artifact:
        payload = artifact.read_bytes()
        result["artifact"] = {
            "path": str(artifact),
            "size_bytes": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
        }
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument("--commit", required=True)
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    manifest = build_manifest(args.repo_root.resolve(), args.commit, args.artifact)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
