import copy
import importlib.util
import pathlib
import unittest

path = pathlib.Path(__file__).resolve().parents[2] / "tools/check_ui_runtime_budget.py"
spec = importlib.util.spec_from_file_location("budget", path)
budget = importlib.util.module_from_spec(spec)
spec.loader.exec_module(budget)


class BudgetTest(unittest.TestCase):
    def setUp(self):
        self.record = dict(board="JC4880", source_sha="a" * 40, scenario="dual-playback",
            status=dict(deck1=dict(playing=True), deck2=dict(playing=True),
                        diagnostics=dict(internal_free=50000, internal_largest_free=24000,
                                        dma_free=20000, dma_largest_free=12000)),
            resources=dict(critical_allocation_failures=0, startup_phase="ready",
                           stack_min_bytes=[2048] * 9, stack_sample_ms=[12345] * 9))

    def test_threshold_and_valid_scenarios(self):
        candidate = copy.deepcopy(self.record)
        candidate["status"]["diagnostics"]["internal_free"] = 45000
        self.assertEqual(budget.check(self.record, candidate), [])
        candidate["status"]["diagnostics"]["internal_free"] -= 1
        self.assertTrue(budget.check(self.record, candidate))

    def test_missing_corrupt_and_failed_evidence(self):
        for field, value in (("critical_allocation_failures", 1), ("startup_phase", "ui"),
                             ("stack_min_bytes", [0] * 9), ("stack_sample_ms", [0] * 9)):
            candidate = copy.deepcopy(self.record)
            candidate["resources"][field] = value
            self.assertTrue(budget.check(self.record, candidate))
        candidate = copy.deepcopy(self.record)
        del candidate["status"]["diagnostics"]["internal_largest_free"]
        self.assertTrue(budget.check(self.record, candidate))
        candidate = copy.deepcopy(self.record)
        candidate["board"] = "JC1060"
        self.assertTrue(budget.check(self.record, candidate))
        candidate = copy.deepcopy(self.record)
        candidate["source_sha"] = "short"
        self.assertTrue(budget.check(self.record, candidate))


if __name__ == "__main__":
    unittest.main()
