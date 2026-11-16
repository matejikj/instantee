#!/usr/bin/env python3
"""Check that the tracker matches the local backlog revision, slices and task set."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
TRACKER = REPO_ROOT / "IMPLEMENTATION_STATUS.json"
EXIT_SKIPPED = 3


def backlog_slices(tasks: list[dict]) -> dict[str, list[str]]:
    slices: dict[str, list[str]] = {}
    for task in tasks:
        if task.get("delivery_slice"):
            slices.setdefault(task["delivery_slice"], []).append(task["id"])
    return slices


def tracker_slices(tracker: dict) -> dict[str, list[str]]:
    slices: dict[str, list[str]] = {}
    for phase, entries in tracker.get("delivery_slices", {}).items():
        if phase == "policy":
            continue
        for entry in entries:
            slices[entry["id"]] = list(entry["tasks"])
    return slices


def find_drift(tracker: dict, tasks: list[dict], digest: str) -> list[str]:
    """Return human-readable differences between the tracker and the backlog."""
    problems = []
    if tracker["current_backlog"]["sha256"] != digest:
        problems.append("current_backlog.sha256 does not match the backlog file")
    tracked, planned = set(tracker["tasks"]), {task["id"] for task in tasks}
    if planned - tracked:
        problems.append("tasks missing from tracker: " + ", ".join(sorted(planned - tracked)))
    if tracked - planned:
        problems.append("tracker tasks not in backlog: " + ", ".join(sorted(tracked - planned)))
    expected, recorded = backlog_slices(tasks), tracker_slices(tracker)
    if expected.keys() != recorded.keys():
        problems.append(
            f"slice IDs differ: backlog {sorted(expected)}, tracker {sorted(recorded)}"
        )
    for slice_id in expected.keys() & recorded.keys():
        if set(expected[slice_id]) != set(recorded[slice_id]):
            problems.append(f"{slice_id}: backlog {expected[slice_id]}, tracker {recorded[slice_id]}")
    return problems


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--write-digest", action="store_true",
        help="record the current backlog digest after reviewing the backlog change",
    )
    args = parser.parse_args(argv)
    tracker = json.loads(TRACKER.read_text(encoding="utf-8"))
    backlog = REPO_ROOT / tracker["current_backlog"]["path"]
    if not backlog.is_file():
        print("skipped: local backlog is not present")
        return EXIT_SKIPPED
    raw = backlog.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if args.write_digest:
        tracker["current_backlog"]["sha256"] = digest
        TRACKER.write_text(json.dumps(tracker, indent=2) + "\n", encoding="utf-8")
    problems = find_drift(tracker, json.loads(raw)["tasks"], digest)
    for problem in problems:
        print(f"drift: {problem}")
    if not problems:
        print("tracker matches backlog")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
