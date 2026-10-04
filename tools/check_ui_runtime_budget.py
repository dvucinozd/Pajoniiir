"""Compare operator-captured, identical UI scenarios. Missing evidence fails closed.

Input files contain board, source_sha, scenario, status (/api/status JSON), and
resources (/api/resources JSON). This validates memory evidence, not sound.
"""
import argparse
import json

TASKS = ("output", "decode1", "decode2", "loader1", "loader2", "lvgl", "controller", "storage", "http")


def check(baseline, candidate):
    failures = []
    if candidate.get("scenario") not in ("network-usb-idle", "dual-playback"):
        failures.append("scenario must be network-usb-idle or dual-playback")
    for field in ("board", "scenario"):
        if not baseline.get(field) or baseline.get(field) != candidate.get(field):
            failures.append(f"{field}: scenarios must match")
    for name, record in (("baseline", baseline), ("candidate", candidate)):
        sha = record.get("source_sha", "")
        if len(sha) != 40 or any(c not in "0123456789abcdef" for c in sha):
            failures.append(f"{name}: full source SHA required")
        if record.get("scenario") == "dual-playback" and not all(
                record.get("status", {}).get(deck, {}).get("playing") is True
                for deck in ("deck1", "deck2")):
            failures.append(f"{name}: dual playback must actually be active")
    b = baseline.get("status", {}).get("diagnostics", {})
    c = candidate.get("status", {}).get("diagnostics", {})
    # Absolute reserves cover an 8 KiB internal task plus allocator/USB margin.
    for field, minimum in (("internal_free", 24576), ("internal_largest_free", 12288),
                           ("dma_free", 8192), ("dma_largest_free", 4096)):
        value = c.get(field)
        if not isinstance(value, int) or value < minimum:
            failures.append(f"{field}: requires >= {minimum} bytes")
    for field in ("internal_free", "internal_largest_free"):
        old, new = b.get(field), c.get(field)
        if not isinstance(old, int) or old <= 0 or not isinstance(new, int):
            failures.append(f"{field}: baseline measurement missing")
        elif new * 10 < old * 9:
            failures.append(f"{field}: regression exceeds 10 percent")
    r = candidate.get("resources", {})
    if r.get("critical_allocation_failures") != 0:
        failures.append("critical allocation failures must be zero")
    if r.get("startup_phase") != "ready":
        failures.append("startup not ready")
    reserves, samples = r.get("stack_min_bytes", []), r.get("stack_sample_ms", [])
    required = (0, 5, 6, 7, 8)
    if candidate.get("scenario") == "dual-playback":
        required = tuple(range(len(TASKS)))
    for i in required:
        minimum = 1024 if i in (0, 8) else 512
        if (len(reserves) <= i or len(samples) <= i or not samples[i] or
                not isinstance(reserves[i], int) or reserves[i] < minimum):
            failures.append(f"{TASKS[i]}: missing sample or stack reserve < {minimum} bytes")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline")
    parser.add_argument("candidate")
    args = parser.parse_args()
    with open(args.baseline, encoding="utf-8-sig") as f:
        baseline = json.load(f)
    with open(args.candidate, encoding="utf-8-sig") as f:
        candidate = json.load(f)
    failures = check(baseline, candidate)
    for failure in failures:
        print("FAIL", failure)
    print("FAIL" if failures else "PASS", "UI runtime resource evidence")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
