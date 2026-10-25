"""Keep removed sources from breaking reports without hiding active uncovered code."""

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/coverage_notes.py"
SPEC = importlib.util.spec_from_file_location("coverage_notes", SCRIPT)
notes = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(notes)


class CoverageNotesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.build = self.root / "coverage build"
        self.build.mkdir()
        self.archive = self.root / "reports" / "orphaned-notes"
        self.active = self.build / "CMakeFiles" / "Active.cpp.obj"
        self.unexecuted = self.build / "CMakeFiles" / "Unexecuted.cpp.obj"
        self.objects = {self.active, self.unexecuted}

    def counter(self, name, content=b"fixture"):
        path = self.build / "CMakeFiles" / name
        path.parent.mkdir(exist_ok=True)
        path.write_bytes(content)
        return path

    def test_preserves_executed_and_unexecuted_active_units(self):
        active = self.counter("Active.cpp.gcno")
        data = self.counter("Active.cpp.gcda")
        unexecuted = self.counter("Unexecuted.cpp.gcno")
        result = notes.quarantine_notes(self.build, self.objects, self.archive)
        self.assertEqual(result, {"activeObjects": 2, "orphanedCounters": []})
        self.assertTrue(all(p.exists() for p in (active, data, unexecuted)))
        self.assertFalse(self.archive.exists())

    def test_quarantines_only_orphans_and_retains_original_bytes(self):
        current = self.counter("Unexecuted.cpp.gcno")
        old = [self.counter("Removed.cpp.gcno", b"old notes"),
               self.counter("Removed.cpp.gcda", b"old counters")]
        result = notes.quarantine_notes(self.build, self.objects, self.archive)
        self.assertEqual(len(result["orphanedCounters"]), 2)
        self.assertTrue(current.exists())
        self.assertFalse(any(p.exists() for p in old))
        preserved = {Path(p["preserved"]).read_bytes() for p in result["orphanedCounters"]}
        self.assertEqual(preserved, {b"old notes", b"old counters"})
        again = notes.quarantine_notes(self.build, self.objects, self.archive)
        self.assertEqual(again["orphanedCounters"], [])

    def test_orphan_data_without_notes_is_preserved(self):
        old = self.counter("Removed.cpp.gcda")
        result = notes.quarantine_notes(self.build, self.objects, self.archive)
        self.assertFalse(old.exists())
        self.assertEqual(result["orphanedCounters"][0]["original"], "CMakeFiles/Removed.cpp.gcda")

    def test_repeated_orphans_never_overwrite_prior_evidence(self):
        self.counter("Removed.cpp.gcno", b"first")
        first = notes.quarantine_notes(self.build, self.objects, self.archive)
        self.counter("Removed.cpp.gcno", b"second")
        second = notes.quarantine_notes(self.build, self.objects, self.archive)
        self.assertNotEqual(first["orphanedCounters"], second["orphanedCounters"])
        self.assertEqual(Path(first["orphanedCounters"][0]["preserved"]).read_bytes(), b"first")

    def test_parses_native_targets_with_spaces_and_ignores_other_build_rules(self):
        output = "CMakeFiles/Active.cpp.obj: CXX_COMPILER\nwith space/Second.cpp.o: CXX_COMPILER\nall: phony\n"
        self.assertEqual(notes.object_targets(self.build, output),
                         {self.active, self.build / "with space" / "Second.cpp.o"})

    def test_missing_or_external_objects_fail_without_moving_counters(self):
        old = self.counter("Removed.cpp.gcno")
        for output in ("all: phony", "../outside.cpp.obj: CXX_COMPILER"):
            with self.assertRaises(ValueError):
                notes.object_targets(self.build, output)
        for objects in (set(), {self.root / "outside.cpp.obj"}):
            with self.assertRaises(ValueError):
                notes.quarantine_notes(self.build, objects, self.archive)
        self.assertTrue(old.exists())

    def test_archive_cannot_remain_in_collected_build(self):
        old = self.counter("Removed.cpp.gcno")
        with self.assertRaises(ValueError):
            notes.quarantine_notes(self.build, self.objects, self.build / "archive")
        self.assertTrue(old.exists())

    def test_counter_alias_outside_build_aborts_the_entire_preflight(self):
        outside = self.root / "outside.gcno"
        outside.write_bytes(b"outside")
        old = self.counter("Removed.cpp.gcno")
        link = self.build / "CMakeFiles" / "ZAlias.cpp.gcno"
        try:
            link.symlink_to(outside)
        except OSError as error:
            self.skipTest(f"File symlinks unavailable: {error}")
        with self.assertRaises(ValueError):
            notes.quarantine_notes(self.build, self.objects, self.archive)
        self.assertTrue(old.exists())
        self.assertEqual(outside.read_bytes(), b"outside")

    def test_wrong_build_or_ninja_failure_never_moves_notes(self):
        old = self.counter("Removed.cpp.gcno")
        with self.assertRaises(ValueError):
            notes.collect(self.build, "unused", self.archive)
        (self.build / "build.ninja").write_text("# fixture")
        (self.build / "CMakeCache.txt").write_text("CONFLUX_ENABLE_COVERAGE:BOOL=ON\n")
        with patch.object(notes.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "ninja")):
            with self.assertRaises(subprocess.CalledProcessError):
                notes.collect(self.build, "unused", self.archive)
        self.assertTrue(old.exists())

    def test_disabled_or_commented_coverage_flag_cannot_select_a_normal_build(self):
        old = self.counter("Removed.cpp.gcno")
        (self.build / "build.ninja").write_text("# fixture")
        for value in ("CONFLUX_ENABLE_COVERAGE:BOOL=OFF", "#CONFLUX_ENABLE_COVERAGE:BOOL=ON"):
            (self.build / "CMakeCache.txt").write_text(value)
            with patch.object(notes.subprocess, "run") as process:
                with self.assertRaises(ValueError):
                    notes.collect(self.build, "unused", self.archive)
                process.assert_not_called()
        self.assertTrue(old.exists())

    def test_cli_uses_current_ninja_metadata(self):
        ninja = shutil.which("ninja")
        installed = Path("C:/Qt/Tools/Ninja/ninja.exe")
        if not ninja and installed.is_file():
            ninja = str(installed)
        if not ninja:
            self.skipTest("Ninja is unavailable")
        (self.build / "CMakeCache.txt").write_text("CONFLUX_ENABLE_COVERAGE:BOOL=ON\n")
        (self.build / "build.ninja").write_text(
            "rule compile\n  command = unused\nbuild CMakeFiles/Active.cpp.obj: compile\n")
        self.counter("Active.cpp.gcno")
        self.counter("Removed.cpp.gcno")
        output = self.root / "notes.json"
        result = subprocess.run([sys.executable, str(SCRIPT), "--build", str(self.build),
                                 "--ninja", ninja, "--quarantine", str(self.archive), "--output", str(output)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(output.read_text())
        self.assertEqual(report["activeObjects"], 1)
        self.assertEqual(len(report["orphanedCounters"]), 1)
        self.assertTrue((self.build / "CMakeFiles" / "Active.cpp.gcno").exists())


if __name__ == "__main__":
    unittest.main()
