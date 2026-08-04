from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "validate_p4_dual_usb_log.py"
SPEC = importlib.util.spec_from_file_location("validator", MODULE_PATH)
assert SPEC and SPEC.loader
validator = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = validator
SPEC.loader.exec_module(validator)


def status(*, dual=1800, class_mask="03", msc_active=1, msc_conn=1,
           msc_disc=0, read_ok=7200, read_fail=0, midi_active=1,
           midi_conn=1, midi_disc=0, packets=500, bytes_=2000,
           reject=0, submit_fail=0, msc_drop=0, probe_drop=0):
    return (
        "W PHASE1 STATUS uptime=1900s "
        f"dual={dual}s host_rc=OK devices=2 clients=2 "
        f"class_mask=0x{class_mask} "
        f"MSC(active={msc_active} conn={msc_conn} disc={msc_disc} "
        f"read_ok={read_ok} read_fail={read_fail}) "
        f"MIDI(active={midi_active} conn={midi_conn} disc={midi_disc} "
        f"packets={packets} bytes={bytes_} reject={reject} "
        f"submit_fail={submit_fail}) "
        "topology(msc_parent=1 midi_parent=1 root_mask=0x00000002) "
        f"drops(msc={msc_drop} probe={probe_drop}) bna_recovered=0"
    )


class ValidatorTests(unittest.TestCase):
    def run_validation(self, text, matrix=False, soak=1800):
        summary, errors = validator.analyse(text)
        return validator.validate(
            summary,
            errors,
            required_soak_seconds=soak,
            require_disconnect_matrix=matrix,
        )

    @staticmethod
    def prefix():
        return "peripheral_map=0x03\nMSC READY\nMIDI READY\n"

    def test_accepts_clean_soak(self):
        self.assertEqual([], self.run_validation(self.prefix() + status()))

    def test_rejects_missing_activity(self):
        failures = "\n".join(self.run_validation(
            self.prefix() + status(dual=10, read_ok=0, packets=0)
        ))
        self.assertIn("MSC read counter", failures)
        self.assertIn("MIDI packet counter", failures)
        self.assertIn("below 1800s", failures)

    def test_rejects_fatal_and_queue_drops(self):
        failures = "\n".join(self.run_validation(
            self.prefix() + "Guru Meditation Error\n" + status(msc_drop=1)
        ))
        self.assertIn("guru_meditation", failures)
        self.assertIn("queue drops", failures)

    def test_disconnect_matrix(self):
        self.assertEqual([], self.run_validation(
            self.prefix() + status(msc_conn=2, msc_disc=1,
                                   midi_conn=2, midi_disc=1),
            matrix=True,
        ))

    def test_disconnect_matrix_requires_recovery(self):
        self.assertGreaterEqual(
            len(self.run_validation(self.prefix() + status(), matrix=True)),
            3,
        )


if __name__ == "__main__":
    unittest.main()
