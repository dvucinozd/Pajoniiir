from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load(name: str, relative: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader
    spec.loader.exec_module(module)
    return module


budget = load("budget", "tools/check_p4_binary_budget.py")
manifest = load("manifest", "tools/create_p4_acceptance_manifest.py")


class EvidenceToolsTest(unittest.TestCase):
    def test_budget_boundary(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "app.bin"
            binary.write_bytes(b"x" * 16)
            self.assertTrue(budget.inspect_binary(binary, 16)["within_budget"])
            report = budget.inspect_binary(binary, 15)
            self.assertFalse(report["within_budget"])
            self.assertEqual(report["remaining_bytes"], -1)

    def test_budget_cli_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "app.bin"
            binary.write_bytes(b"x" * 8)
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools/check_p4_binary_budget.py"),
                    "--binary", str(binary),
                    "--max-bytes", "7",
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 2)
            self.assertFalse(json.loads(result.stdout)["within_budget"])

    def test_manifest_is_deterministic_and_marks_hardware_open(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for relative in manifest.LOCK_PATHS:
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(relative, encoding="utf-8")
            feature = root / manifest.FEATURE_PATH
            feature.parent.mkdir(parents=True, exist_ok=True)
            feature.write_text(
                "CONFIG_PAJONIIIR_P4_LOCAL_CONTROLLER=y\n",
                encoding="utf-8",
            )
            first = manifest.build_manifest(root, "a" * 40, None)
            second = manifest.build_manifest(root, "a" * 40, None)
            self.assertEqual(first, second)
            self.assertFalse(first["hardware_accepted"])
            self.assertEqual(len(first["dependency_locks"]), 3)

    def test_manifest_rejects_invalid_commit(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(ValueError):
                manifest.build_manifest(Path(temp), "not-a-commit", None)


if __name__ == "__main__":
    unittest.main()
