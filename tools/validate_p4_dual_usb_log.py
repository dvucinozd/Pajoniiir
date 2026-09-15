#!/usr/bin/env python3
"""Validate a Pajoniiir Phase 1 P4 dual-USB serial log."""
from __future__ import annotations

import argparse
import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path

STATUS_RE = re.compile(
    r"PHASE1 STATUS .*?dual=(?P<dual>\d+)s .*?"
    r"class_mask=0x(?P<class_mask>[0-9A-Fa-f]+) "
    r"MSC\(active=(?P<msc_active>\d+) conn=(?P<msc_conn>\d+) "
    r"disc=(?P<msc_disc>\d+) read_ok=(?P<read_ok>\d+) "
    r"read_fail=(?P<read_fail>\d+)\) "
    r"MIDI\(active=(?P<midi_active>\d+) conn=(?P<midi_conn>\d+) "
    r"disc=(?P<midi_disc>\d+) packets=(?P<packets>\d+) "
    r"bytes=(?P<bytes>\d+) reject=(?P<reject>\d+) "
    r"submit_fail=(?P<submit_fail>\d+)\) "
    r"topology\(msc_parent=(?P<msc_parent>\d+) "
    r"msc_direct=(?P<msc_direct>\d+) "
    r"midi_parent=(?P<midi_parent>\d+) "
    r"midi_direct=(?P<midi_direct>\d+) "
    r"root_mask=0x(?P<root_mask>[0-9A-Fa-f]+)\).*?"
    r"drops\(msc=(?P<msc_drop>\d+) probe=(?P<probe_drop>\d+)\)"
)

FATAL_PATTERNS = {
    "guru_meditation": re.compile(r"Guru Meditation", re.IGNORECASE),
    "panic": re.compile(r"\bpanic(?:ked)?\b", re.IGNORECASE),
    "watchdog": re.compile(r"watchdog|task_wdt|interrupt wdt", re.IGNORECASE),
    "assert": re.compile(r"assert(?:ion)? failed|abort\(\)", re.IGNORECASE),
    "brownout": re.compile(r"brownout", re.IGNORECASE),
    "stack_overflow": re.compile(r"stack overflow", re.IGNORECASE),
}


@dataclass
class Summary:
    statuses: int = 0
    max_dual_seconds: int = 0
    max_valid_topology_dual_seconds: int = 0
    max_class_mask: int = 0
    max_msc_connects: int = 0
    max_msc_disconnects: int = 0
    max_msc_reads_ok: int = 0
    max_msc_reads_failed: int = 0
    max_midi_connects: int = 0
    max_midi_disconnects: int = 0
    max_midi_packets: int = 0
    max_midi_bytes: int = 0
    max_midi_submit_failures: int = 0
    max_msc_event_drops: int = 0
    max_probe_event_drops: int = 0
    saw_host_dual_map: bool = False
    saw_msc_ready: bool = False
    saw_midi_ready: bool = False
    saw_dual_active_after_reconnect: bool = False
    saw_expected_topology: bool = False


def analyse(text: str):
    summary = Summary(
        saw_host_dual_map=bool(
            re.search(r"peripheral_map=0x0*3\b", text, re.IGNORECASE)
        ),
        saw_msc_ready="MSC READY" in text,
        saw_midi_ready="MIDI READY" in text,
    )
    errors = [
        f"fatal marker found: {name}"
        for name, pattern in FATAL_PATTERNS.items()
        if pattern.search(text)
    ]

    for match in STATUS_RE.finditer(text):
        values = {
            key: int(value, 16 if key in {"class_mask", "root_mask"} else 10)
            for key, value in match.groupdict().items()
        }
        summary.statuses += 1
        summary.max_dual_seconds = max(summary.max_dual_seconds,
                                       values["dual"])
        summary.max_class_mask |= values["class_mask"]
        summary.max_msc_connects = max(summary.max_msc_connects,
                                       values["msc_conn"])
        summary.max_msc_disconnects = max(summary.max_msc_disconnects,
                                          values["msc_disc"])
        summary.max_msc_reads_ok = max(summary.max_msc_reads_ok,
                                       values["read_ok"])
        summary.max_msc_reads_failed = max(summary.max_msc_reads_failed,
                                           values["read_fail"])
        summary.max_midi_connects = max(summary.max_midi_connects,
                                        values["midi_conn"])
        summary.max_midi_disconnects = max(summary.max_midi_disconnects,
                                           values["midi_disc"])
        summary.max_midi_packets = max(summary.max_midi_packets,
                                       values["packets"])
        summary.max_midi_bytes = max(summary.max_midi_bytes,
                                     values["bytes"])
        summary.max_midi_submit_failures = max(
            summary.max_midi_submit_failures, values["submit_fail"]
        )
        summary.max_msc_event_drops = max(summary.max_msc_event_drops,
                                          values["msc_drop"])
        summary.max_probe_event_drops = max(summary.max_probe_event_drops,
                                             values["probe_drop"])
        valid_topology = (
            values["msc_parent"] == 0
            and values["msc_direct"] == 1
            and values["midi_parent"] == 1
            and values["midi_direct"] == 1
            and (values["root_mask"] & 0x03) == 0x03
        )
        if valid_topology:
            summary.saw_expected_topology = True
            summary.max_valid_topology_dual_seconds = max(
                summary.max_valid_topology_dual_seconds,
                values["dual"],
            )
        if (
            values["msc_conn"] >= 2
            and values["midi_conn"] >= 2
            and values["msc_active"] == 1
            and values["midi_active"] == 1
            and valid_topology
        ):
            summary.saw_dual_active_after_reconnect = True

    return summary, errors


def validate(summary, errors, *, required_soak_seconds,
             require_disconnect_matrix):
    failures = list(errors)
    if not summary.saw_host_dual_map:
        failures.append("missing dual peripheral_map=0x03 evidence")
    if not summary.saw_msc_ready:
        failures.append("missing MSC READY evidence")
    if not summary.saw_midi_ready:
        failures.append("missing MIDI READY evidence")
    if summary.statuses == 0:
        failures.append("no parsable PHASE1 STATUS lines")
        return failures
    if (summary.max_class_mask & 0x03) != 0x03:
        failures.append("class_mask never showed both MSC and MIDI")
    if not summary.saw_expected_topology:
        failures.append(
            "expected direct topology USB0=MSC and USB1=MIDI was never observed"
        )
    if summary.max_msc_reads_ok == 0:
        failures.append("MSC read counter never advanced")
    if summary.max_midi_packets == 0:
        failures.append("MIDI packet counter never advanced")
    if summary.max_valid_topology_dual_seconds < required_soak_seconds:
        failures.append(
            "valid-topology dual-active soak "
            f"{summary.max_valid_topology_dual_seconds}s is below "
            f"{required_soak_seconds}s"
        )
    if summary.max_msc_reads_failed:
        failures.append(
            f"MSC read failures observed: {summary.max_msc_reads_failed}"
        )
    if summary.max_midi_submit_failures:
        failures.append(
            "MIDI submit failures observed: "
            f"{summary.max_midi_submit_failures}"
        )
    if summary.max_msc_event_drops or summary.max_probe_event_drops:
        failures.append(
            "USB event queue drops observed: "
            f"msc={summary.max_msc_event_drops} "
            f"probe={summary.max_probe_event_drops}"
        )
    if require_disconnect_matrix:
        if summary.max_msc_connects < 2 or summary.max_msc_disconnects < 1:
            failures.append("MSC disconnect/reconnect cycle not proven")
        if summary.max_midi_connects < 2 or summary.max_midi_disconnects < 1:
            failures.append("MIDI disconnect/reconnect cycle not proven")
        if not summary.saw_dual_active_after_reconnect:
            failures.append("dual-active state after both reconnects not proven")
    return failures


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("--require-soak-seconds", type=int, default=1800)
    parser.add_argument("--require-disconnect-matrix", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    text = args.log.read_text(encoding="utf-8", errors="replace")
    summary, errors = analyse(text)
    failures = validate(
        summary,
        errors,
        required_soak_seconds=args.require_soak_seconds,
        require_disconnect_matrix=args.require_disconnect_matrix,
    )
    report = {
        "accepted": not failures,
        "summary": asdict(summary),
        "failures": failures,
    }
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print("PASS" if not failures else "FAIL")
        print(json.dumps(asdict(summary), indent=2, sort_keys=True))
        for failure in failures:
            print(f"- {failure}")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
