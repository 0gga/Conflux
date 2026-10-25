"""Protect the audit denominator and freshness gates from misleading success."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/coverage_audit.py"
SPEC = importlib.util.spec_from_file_location("coverage_audit", SCRIPT)
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class CoverageAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ("src/domain/Policy.cpp", "src/domain/Policy.h", "src/platform/Storage.cpp",
                     "src/theme/Theme.cpp", "src/main.cpp", "src/verification/Smoke.cpp", "qml/Main.qml"):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("// fixture\n", encoding="utf-8")
        self.inventory = audit.source_inventory(self.root)
        self.ledger = audit.sync_ledger(self.inventory, {})
        self.summary = {"files": [self.record("src/domain/Policy.cpp", 10, 10)]}

    @staticmethod
    def record(name, total, covered):
        return {"filename": name, "line_total": total, "line_covered": covered,
                "branch_total": total * 2, "branch_covered": covered * 2,
                "function_total": 1, "function_covered": int(covered > 0)}

    def report(self, snapshot=None):
        return audit.build_report(self.root, self.inventory, self.ledger, self.summary, snapshot)

    def test_missing_implementation_cannot_disappear_from_complete_coverage_gate(self):
        report = self.report(self.inventory)
        counts = report["phases"]["non-ui"]
        self.assertEqual(counts["files"], 3)
        self.assertEqual(counts["line"]["percent"], 100)
        self.assertEqual(counts["unmeasuredCpp"], ["src/platform/Storage.cpp"])
        self.assertTrue(audit.gate_failures(report, "non-ui", 100, 100, False))
        # Declaration-only headers have no invented executable-line count.
        self.assertEqual(report["files"]["src/domain/Policy.h"]["measurement"], "unmeasured")

    def test_aggregate_is_weighted_by_executable_lines_not_file_percentages(self):
        self.summary["files"].append(self.record("src/platform/Storage.cpp", 30, 0))
        counts = self.report(self.inventory)["phases"]["non-ui"]
        self.assertEqual(counts["line"], {"total": 40, "covered": 10, "percent": 25})
        self.assertEqual(counts["branch"]["percent"], 25)

    def test_completed_measured_phase_passes_but_stale_snapshot_fails(self):
        self.summary["files"].append(self.record("src/platform/Storage.cpp", 4, 4))
        for row in self.ledger["files"].values():
            row["status"] = "reviewed"
            row["checks"] = ["Read boundaries and regression tests."]
        self.assertEqual(audit.gate_failures(self.report(self.inventory), "non-ui", 100, 100, True), [])
        self.assertTrue(audit.gate_failures(self.report(), "non-ui", 100, 0, False))
        stale = dict(self.inventory)
        stale["src/platform/Storage.cpp"] = {"phase": "non-ui", "sha256": "old"}
        self.assertTrue(audit.gate_failures(self.report(stale), "non-ui", 100, 0, False))

    def test_changed_source_invalidates_review_and_sync_does_not_reapprove_it(self):
        row = self.ledger["files"]["src/domain/Policy.cpp"]
        row.update(status="reviewed", checks=["Review complete."])
        self.assertEqual(self.report()["files"]["src/domain/Policy.cpp"]["review"], "reviewed")
        (self.root / "src/domain/Policy.cpp").write_text("changed", encoding="utf-8")
        self.inventory = audit.source_inventory(self.root)
        self.ledger = audit.sync_ledger(self.inventory, self.ledger)
        report = self.report()
        self.assertEqual(report["files"]["src/domain/Policy.cpp"]["review"], "stale")
        self.assertTrue(audit.gate_failures(report, "non-ui", 0, 0, True))

    def test_unsubstantiated_review_and_reclassification_are_stale(self):
        row = self.ledger["files"]["src/domain/Policy.cpp"]
        row["status"] = "reviewed"
        self.assertEqual(self.report()["files"]["src/domain/Policy.cpp"]["review"], "stale")
        row.update(checks=["Read."], phase="ui")
        self.assertEqual(self.report()["files"]["src/domain/Policy.cpp"]["review"], "stale")

    def test_qml_cannot_be_reported_as_cpp_test_coverage(self):
        report = self.report(self.inventory)
        self.assertEqual(report["phases"]["ui"]["unmeasuredQmlOrJs"], ["qml/Main.qml"])
        self.assertTrue(audit.gate_failures(report, "ui", 100, 0, False))
        self.assertEqual(report["phases"]["verification"]["files"], 1)

    def test_removed_new_and_outside_files_remain_explicit(self):
        self.ledger["files"]["src/removed.cpp"] = {"status": "reviewed"}
        self.summary["files"].append(self.record("third_party/vendor.cpp", 999, 999))
        del self.ledger["files"]["src/domain/Policy.h"]
        report = self.report()
        self.assertEqual(report["obsoleteLedgerFiles"], ["src/removed.cpp"])
        self.assertEqual(report["coverageFilesOutsideInventory"], ["third_party/vendor.cpp"])
        self.assertEqual(report["phases"]["non-ui"]["line"]["total"], 10)
        self.assertEqual(report["files"]["src/domain/Policy.h"]["review"], "pending")
        self.assertTrue(audit.gate_failures(report, "non-ui", 0, 0, False))

    def test_duplicate_or_invalid_counters_fail_closed(self):
        self.summary["files"].append(self.summary["files"][0])
        with self.assertRaises(ValueError):
            self.report()
        self.summary["files"].pop()
        for invalid in (-1, 11, 1.5, "10", True):
            self.summary["files"][0]["line_covered"] = invalid
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                self.report()

    def test_native_and_absolute_report_paths_are_joined(self):
        self.summary["files"][0]["filename"] = str(self.root / "src/domain/Policy.cpp").replace("/", "\\")
        self.assertEqual(self.report()["phases"]["non-ui"]["line"]["covered"], 10)
        self.summary["files"][0]["filename"] = "./src/domain/Policy.cpp"
        self.assertEqual(self.report()["phases"]["non-ui"]["line"]["covered"], 10)

    def test_cli_preserves_reviews_and_refuses_stale_build_snapshot(self):
        command = [sys.executable, str(SCRIPT), "--root", str(self.root)]
        snapshot = self.root / "snapshot.json"
        summary = self.root / "summary.json"
        output = self.root / "audit.json"
        summary.write_text(json.dumps(self.summary), encoding="utf-8")
        result = subprocess.run(command + ["--sync-ledger", "--write-snapshot", str(snapshot)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run(command + ["--verify-snapshot", str(snapshot), "--summary", str(summary),
                                          "--snapshot", str(snapshot), "--output", str(output)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(output.read_text())["phases"]["non-ui"]["files"], 3)
        (self.root / "src/domain/Policy.cpp").write_text("new", encoding="utf-8")
        result = subprocess.run(command + ["--verify-snapshot", str(snapshot)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        result = subprocess.run(command + ["--fail-under-line", "101", "--sync-ledger"], capture_output=True)
        self.assertNotEqual(result.returncode, 0)

    def test_build_snapshot_also_invalidates_test_and_configuration_changes(self):
        tests = self.root / "tests"
        tests.mkdir()
        (tests / "PolicyTests.cpp").write_text("old test", encoding="utf-8")
        cmake = self.root / "CMakeLists.txt"
        cmake.write_text("old build", encoding="utf-8")
        initial = audit.build_snapshot(self.root, self.inventory)
        (tests / "PolicyTests.cpp").write_text("new test", encoding="utf-8")
        self.assertNotEqual(initial, audit.build_snapshot(self.root, self.inventory))
        (tests / "PolicyTests.cpp").write_text("old test", encoding="utf-8")
        self.assertEqual(initial, audit.build_snapshot(self.root, self.inventory))
        cmake.write_text("new build", encoding="utf-8")
        self.assertNotEqual(initial, audit.build_snapshot(self.root, self.inventory))


if __name__ == "__main__":
    unittest.main()
