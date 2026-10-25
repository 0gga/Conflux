"""Preserve obsolete GCC counters outside collection using current Ninja objects.

Unexecuted active units deliberately retain their notes and remain in the denominator.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import uuid


def object_targets(build: Path, output: str) -> set[Path]:
    build = build.resolve(strict=True)
    objects = set()
    for line in output.splitlines():
        target = line.rsplit(": ", 1)[0]
        if not target.endswith((".obj", ".o")):
            continue
        path = (build / target).resolve()
        if not path.is_relative_to(build):
            raise ValueError(f"Compiled object escapes the coverage build: {target}")
        objects.add(path)
    if not objects:
        raise ValueError("No compiled objects in current Ninja metadata; no counters were moved.")
    return objects


def quarantine_notes(build: Path, objects: set[Path], archive: Path) -> dict:
    build = build.resolve(strict=True)
    archive = archive.resolve()
    if archive.is_relative_to(build):
        raise ValueError("The compiler-note archive must be outside the scanned coverage build.")
    if not objects or any(not path.is_relative_to(build) for path in objects):
        raise ValueError("A contained, nonempty current compiled-object inventory is required.")
    # Preflight every path before moving anything; resolve catches file/junction aliases.
    obsolete = []
    for suffix in ("*.gcno", "*.gcda"):
        for candidate in sorted(build.rglob(suffix)):
            resolved = candidate.resolve(strict=True)
            if not resolved.is_relative_to(build):
                raise ValueError(f"Compiler counter escapes the coverage build: {candidate}")
            if not any(resolved.with_suffix(ext) in objects for ext in (".obj", ".o")):
                obsolete.append(resolved)
    run = archive / uuid.uuid4().hex
    moved = []
    for candidate in obsolete:
        destination = (run / candidate.relative_to(build)).resolve()
        if not destination.is_relative_to(archive) or destination.exists():
            raise ValueError("Unsafe or occupied compiler-note archive destination.")
        destination.parent.mkdir(parents=True, exist_ok=True)
        candidate.replace(destination)
        moved.append({"original": candidate.relative_to(build).as_posix(),
                      "preserved": destination.as_posix()})
    return {"activeObjects": len(objects), "orphanedCounters": moved}


def collect(build: Path, ninja: str, archive: Path) -> dict:
    build = build.resolve(strict=True)
    cache = build / "CMakeCache.txt"
    if not (build / "build.ninja").is_file() or not cache.is_file() or \
            "CONFLUX_ENABLE_COVERAGE:BOOL=ON" not in cache.read_text(encoding="utf-8").splitlines():
        raise ValueError("Expected an enabled Conflux CMake/Ninja coverage build.")
    result = subprocess.run([ninja, "-C", str(build), "-t", "targets", "all"],
                            capture_output=True, text=True, check=True)
    return quarantine_notes(build, object_targets(build, result.stdout), archive)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--ninja", required=True)
    parser.add_argument("--quarantine", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = collect(args.build, args.ninja, args.quarantine)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Current compiled objects: {result['activeObjects']}; "
          f"preserved obsolete counters: {len(result['orphanedCounters'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
