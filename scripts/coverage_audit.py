"""Join measured C++ coverage to an exhaustive, hash-checked source review ledger.

QML/JavaScript interaction checks are deliberately not invented as line coverage.
An absent compiler record stays unmeasured, including declaration-only headers.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def source_inventory(root: Path) -> dict[str, dict]:
    files = {}
    for directory in ("src", "qml"):
        for path in sorted((root / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h", ".qml", ".js"):
                continue
            if path.is_symlink() or not path.is_file():
                raise ValueError(f"Source is not a regular file: {path}")
            name = path.relative_to(root).as_posix()
            if name.startswith("src/verification/"):
                phase = "verification"
            elif name.startswith(("qml/", "src/theme/")) or name in {
                "src/main.cpp",
                "src/ApplicationController.cpp", "src/ApplicationController.h",
                "src/DesktopSession.cpp", "src/DesktopSession.h",
                "src/platform/WindowFrame.cpp", "src/platform/WindowFrame.h",
                "src/features/navigation/NavigationInput.cpp",
                "src/features/navigation/NavigationInput.h",
            }:
                phase = "ui"
            else:
                phase = "non-ui"
            files[name] = {
                "phase": phase,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
    return files


def sync_ledger(inventory: dict, ledger: dict) -> dict:
    entries = ledger.get("files", {})
    # Retain reviewed hashes so a source change invalidates a previous review.
    return {
        "schema": 1,
        "files": {
            name: entries.get(name, {**source, "status": "pending", "checks": [], "findings": []})
            for name, source in inventory.items()
        },
    }


def build_snapshot(root: Path, inventory: dict) -> dict:
    # Old tests/build settings must not be relabelled as a new measured run either.
    inputs = {}
    for directory in ("tests", "cmake", "themes"):
        for path in sorted((root / directory).rglob("*")):
            if path.suffix in (".cpp", ".h", ".py", ".cmake", ".json") and path.is_file():
                inputs[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    for name in ("CMakeLists.txt", "CMakePresets.json"):
        path = root / name
        if path.is_file():
            inputs[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    return {"schema": 1, "application": inventory, "buildInputs": inputs}


def source_name(filename: str, root: Path) -> str:
    normalized = filename.replace("\\", "/")
    prefix = root.resolve().as_posix().rstrip("/") + "/"
    if normalized.casefold().startswith(prefix.casefold()):
        normalized = normalized[len(prefix):]
    while normalized.startswith("./"):
        normalized = normalized[2:]
    return normalized


def build_report(root: Path, inventory: dict, ledger: dict, summary: dict, snapshot: dict | None) -> dict:
    measured = {}
    for item in summary.get("files", []):
        name = source_name(item["filename"], root)
        if name in measured:
            raise ValueError(f"Duplicate coverage record: {name}")
        for metric in ("line", "function", "branch"):
            total, covered = item.get(f"{metric}_total", 0), item.get(f"{metric}_covered", 0)
            if type(total) is not int or type(covered) is not int or not 0 <= covered <= total:
                raise ValueError(f"Invalid {metric} counters: {name}")
        measured[name] = item
    rows = {}
    for name, source in inventory.items():
        review = ledger.get("files", {}).get(name, {})
        valid = review.get("sha256") == source["sha256"] and review.get("phase") == source["phase"]
        status = review.get("status", "pending")
        if status == "reviewed" and (not valid or not review.get("checks")):
            status = "stale"
        elif status != "reviewed":
            status = "pending"
        coverage = measured.get(name)
        rows[name] = {
            **source,
            "review": status,
            "checks": review.get("checks", []),
            "findings": review.get("findings", []),
            "measurement": "measured" if coverage else "unmeasured",
            "snapshotMatches": snapshot is not None and snapshot.get(name) == source,
            "coverage": coverage,
        }
    phases = {}
    for phase in ("non-ui", "ui", "verification"):
        entries = {name: row for name, row in rows.items() if row["phase"] == phase}
        counts = {"files": len(entries), "reviewed": sum(r["review"] == "reviewed" for r in entries.values())}
        counts["reviewPercent"] = 100 * counts["reviewed"] / len(entries) if entries else None
        counts["unmeasuredCpp"] = [n for n, r in entries.items() if n.endswith(".cpp") and not r["coverage"]]
        counts["unmeasuredQmlOrJs"] = [n for n in entries if n.endswith((".qml", ".js"))]
        counts["sourceSnapshotCurrent"] = all(r["snapshotMatches"] for r in entries.values()) and bool(entries)
        for metric in ("line", "function", "branch"):
            total = sum((r["coverage"] or {}).get(f"{metric}_total", 0) for r in entries.values())
            covered = sum((r["coverage"] or {}).get(f"{metric}_covered", 0) for r in entries.values())
            counts[metric] = {"total": total, "covered": covered, "percent": 100 * covered / total if total else None}
        phases[phase] = counts
    return {
        "schema": 1,
        "phases": phases,
        "files": rows,
        "obsoleteLedgerFiles": sorted(set(ledger.get("files", {})) - set(inventory)),
        "coverageFilesOutsideInventory": sorted(set(measured) - set(inventory)),
        "qmlLineCoverage": "not measured; interaction and visual evidence are separate",
    }


def gate_failures(report: dict, phase: str, line: float, branch: float, require_reviewed: bool) -> list[str]:
    counts = report["phases"][phase]
    failures = []
    if line > 0 or branch > 0:
        if counts["unmeasuredCpp"]:
            failures.append("Some C++ implementation files have no coverage record.")
        if counts["unmeasuredQmlOrJs"]:
            failures.append("QML/JavaScript line coverage is not measured by GCC.")
        if not counts["sourceSnapshotCurrent"]:
            failures.append("Coverage does not have a matching current source snapshot.")
    for metric, threshold in (("line", line), ("branch", branch)):
        if threshold > 0 and (counts[metric]["percent"] is None or counts[metric]["percent"] < threshold):
            failures.append(f"{phase} {metric} coverage is below {threshold:g}%.")
    if require_reviewed and counts["reviewed"] != counts["files"]:
        failures.append(f"{phase} source review is incomplete or stale.")
    if report["obsoleteLedgerFiles"]:
        failures.append("The review ledger contains removed or renamed files.")
    return failures


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--ledger", type=Path, default=Path("docs/CODE_AUDIT.json"))
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--snapshot", type=Path)
    parser.add_argument("--write-snapshot", type=Path)
    parser.add_argument("--verify-snapshot", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--sync-ledger", action="store_true")
    parser.add_argument("--phase", choices=("non-ui", "ui", "verification"), default="non-ui")
    parser.add_argument("--fail-under-line", type=float, default=0)
    parser.add_argument("--fail-under-branch", type=float, default=0)
    parser.add_argument("--require-reviewed", action="store_true")
    args = parser.parse_args()
    if any(not 0 <= value <= 100 for value in (args.fail_under_line, args.fail_under_branch)):
        parser.error("Coverage thresholds must be between 0 and 100.")
    root = args.root.resolve()
    inventory = source_inventory(root)
    if not inventory:
        parser.error("No application source files found.")
    if args.write_snapshot:
        write_json(args.write_snapshot, build_snapshot(root, inventory))
    if args.verify_snapshot:
        snapshot = json.loads(args.verify_snapshot.read_text(encoding="utf-8"))
        if snapshot != build_snapshot(root, inventory):
            parser.error("Sources changed since the instrumented build. Run coverage without -SkipBuild.")
    ledger_path = root / args.ledger
    ledger = json.loads(ledger_path.read_text(encoding="utf-8")) if ledger_path.exists() else {}
    if args.sync_ledger:
        ledger = sync_ledger(inventory, ledger)
        write_json(ledger_path, ledger)
    if not args.summary:
        if not args.write_snapshot and not args.verify_snapshot and not args.sync_ledger:
            parser.error("Provide --summary, --write-snapshot or --sync-ledger.")
        return 0
    summary = json.loads(args.summary.read_text(encoding="utf-8"))
    snapshot = json.loads(args.snapshot.read_text(encoding="utf-8")) if args.snapshot else None
    if snapshot is not None:
        snapshot = snapshot.get("application", snapshot)
    report = build_report(root, inventory, ledger, summary, snapshot)
    if args.output:
        write_json(args.output, report)
    print(json.dumps(report["phases"], indent=2))
    failures = gate_failures(report, args.phase, args.fail_under_line, args.fail_under_branch, args.require_reviewed)
    for message in failures:
        print(message)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
