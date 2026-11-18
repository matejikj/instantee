#!/usr/bin/env python3
"""Build and validate the SAS §2 requirements coverage matrix.

Maps every FR/NFR to backlog tasks, verification strings, and tracker evidence.
Fails when a MUST requirement lacks task coverage or a release gate is untracked.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SAS = REPO / "motion_framework_sas" / "SOFTWARE_ARCHITECTURE_SPECIFICATION.md"
RTM = REPO / "motion_framework_sas" / "REQUIREMENTS_TRACEABILITY.md"
TRACKER = REPO / "IMPLEMENTATION_STATUS.json"
BACKLOG = REPO / "motion_framework_sas" / "implementation_tasks.json"
DEFAULT_OUT = REPO / "contracts" / "traceability" / "requirements-coverage-v1.json"

# Phase gate IDs from MILESTONES_AND_RELEASE_GATES.md / tracker.phase_gates.
EXPECTED_GATES = {
    "P0": "P0-020",
    "P1": "P1-020",
    "P2": "P2-020",
    "P3": "P3-025",
    "P4": "P4-020",
    "P5": "P5-020",
    "P6": "P6-021",
    "P7": "P7-025",
    "P8": "P8-037",
}

# Stretch / non-blocking targets called out by SAS (not MUST release blockers).
STRETCH_REQUIREMENTS = frozenset({"NFR-PERF-02"})

# RTM table uses backtick-wrapped task lists joined by commas inside one cell.
RTM_ROW = re.compile(
    r"^\| `(?P<id>(?:FR|NFR)-[A-Z0-9-]+)` \| (?P<title>[^|]+?) \| (?P<tasks>.+?) \|\s*$"
)
SAS_FR = re.compile(
    r"^\| (?P<id>FR-\d+) \| (?P<title>[^|]+?) \| (?P<prio>MUST|SHOULD|MAY) \|",
    re.MULTILINE,
)
SAS_NFR = re.compile(
    r"^\| (?P<id>NFR-[A-Z0-9-]+) \| (?P<title>[^|]+?) \|",
    re.MULTILINE,
)
TASK_ID = re.compile(r"P[0-8]-\d{3}")


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def parse_sas_priorities(sas_text: str) -> dict[str, dict]:
    """Return id → {title, priority} for every SAS §2 requirement."""
    requirements: dict[str, dict] = {}
    for match in SAS_FR.finditer(sas_text):
        requirements[match.group("id")] = {
            "title": match.group("title").strip(),
            "priority": match.group("prio"),
        }
    for match in SAS_NFR.finditer(sas_text):
        req_id = match.group("id")
        title = match.group("title").strip()
        priority = "STRETCH" if req_id in STRETCH_REQUIREMENTS else "MUST"
        requirements[req_id] = {"title": title, "priority": priority}
    return requirements


def parse_rtm_tasks(rtm_text: str) -> dict[str, list[str]]:
    """Return id → ordered task refs from the design-pack RTM table."""
    mapping: dict[str, list[str]] = {}
    for line in rtm_text.splitlines():
        match = RTM_ROW.match(line)
        if not match:
            continue
        req_id = match.group("id")
        tasks = TASK_ID.findall(match.group("tasks"))
        if tasks:
            mapping[req_id] = tasks
    return mapping


def task_coverage(
    task_id: str,
    backlog_by_id: dict,
    tracker_tasks: dict,
) -> dict:
    backlog = backlog_by_id.get(task_id)
    tracked = tracker_tasks.get(task_id, {})
    evidence = list(tracked.get("evidence") or [])
    checks = list(tracked.get("checks") or [])
    verification = tracked.get("verification") or (
        backlog.get("verification") if backlog else None
    )
    return {
        "task_id": task_id,
        "in_backlog": backlog is not None,
        "tracker_status": tracked.get("status"),
        "acceptance_met": (tracked.get("acceptance") or {}).get("met"),
        "verification": verification,
        "evidence": evidence,
        "checks": checks,
        "evidenced": bool(evidence),
    }


def build_matrix(
    sas_text: str,
    rtm_text: str,
    backlog: dict,
    tracker: dict,
) -> dict:
    priorities = parse_sas_priorities(sas_text)
    rtm = parse_rtm_tasks(rtm_text)
    backlog_by_id = {task["id"]: task for task in backlog.get("tasks", [])}
    tracker_tasks = tracker.get("tasks", {})

    sas_ids = set(priorities)
    rtm_ids = set(rtm)
    drift = sorted(sas_ids ^ rtm_ids)

    requirements = []
    uncovered_must = []
    for req_id in sorted(sas_ids, key=lambda value: (value.split("-")[0], value)):
        meta = priorities[req_id]
        task_ids = rtm.get(req_id, [])
        tasks = [
            task_coverage(task_id, backlog_by_id, tracker_tasks) for task_id in task_ids
        ]
        unknown = [row["task_id"] for row in tasks if not row["in_backlog"]]
        mapped = bool(task_ids) and not unknown
        priority = meta["priority"]
        blocking = priority == "MUST"
        if blocking and not mapped:
            uncovered_must.append(req_id)
        done_with_evidence = [
            row["task_id"]
            for row in tasks
            if row["tracker_status"] == "done"
            and row["acceptance_met"] is True
            and row["evidenced"]
        ]
        requirements.append(
            {
                "id": req_id,
                "title": meta["title"],
                "priority": priority,
                "blocking_must": blocking,
                "mapped": mapped,
                "task_ids": task_ids,
                "tasks": tasks,
                "unknown_task_refs": unknown,
                "done_evidenced_tasks": done_with_evidence,
                "verifications": sorted(
                    {
                        row["verification"]
                        for row in tasks
                        if isinstance(row.get("verification"), str)
                        and row["verification"].strip()
                    }
                ),
                "evidence_artifacts": sorted(
                    {
                        path
                        for row in tasks
                        for path in row.get("evidence", [])
                        if isinstance(path, str)
                    }
                ),
            }
        )

    phase_gates = tracker.get("phase_gates") or {}
    gates = []
    untracked_gates = []
    for phase, gate_task in EXPECTED_GATES.items():
        entry = phase_gates.get(phase) or {}
        tracked = entry.get("gate_task_id") == gate_task
        if not tracked:
            untracked_gates.append(phase)
        gates.append(
            {
                "phase": phase,
                "expected_gate_task_id": gate_task,
                "tracker_gate_task_id": entry.get("gate_task_id"),
                "tracked": tracked,
                "status": entry.get("status"),
                "decision": entry.get("decision"),
                "evidence": list(entry.get("evidence") or []),
                "checks": list(entry.get("checks") or []),
            }
        )

    accepted = not uncovered_must and not untracked_gates and not drift
    return {
        "schema_version": "1.0.0",
        "kind": "requirements_coverage_matrix",
        "generated_at_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "sources": {
            "sas": "motion_framework_sas/SOFTWARE_ARCHITECTURE_SPECIFICATION.md",
            "rtm": "motion_framework_sas/REQUIREMENTS_TRACEABILITY.md",
            "backlog": tracker.get("current_backlog", {}).get(
                "path", "motion_framework_sas/implementation_tasks.json"
            ),
            "tracker": "IMPLEMENTATION_STATUS.json",
        },
        "counts": {
            "requirements": len(requirements),
            "must": sum(1 for row in requirements if row["priority"] == "MUST"),
            "should": sum(1 for row in requirements if row["priority"] == "SHOULD"),
            "stretch": sum(1 for row in requirements if row["priority"] == "STRETCH"),
            "mapped": sum(1 for row in requirements if row["mapped"]),
            "release_gates": len(gates),
            "tracked_gates": sum(1 for row in gates if row["tracked"]),
        },
        "acceptance": {
            "no_uncovered_must": not uncovered_must,
            "no_untracked_release_gate": not untracked_gates,
            "sas_rtm_ids_aligned": not drift,
            "met": accepted,
        },
        "uncovered_must": uncovered_must,
        "untracked_release_gates": untracked_gates,
        "sas_rtm_id_drift": drift,
        "requirements": requirements,
        "release_gates": gates,
        "notes": [
            "Coverage means every SAS §2 requirement is mapped to backlog tasks and every phase gate is tracked.",
            "Done+evidenced tasks strengthen the matrix; unfinished P8 mappings remain tracked planning coverage.",
            "NFR-PERF-02 is a SAS stretch target, not a blocking MUST release threshold.",
            "FR-14 is SHOULD; mapped but non-blocking if incomplete.",
        ],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sas", type=Path, default=SAS)
    parser.add_argument("--rtm", type=Path, default=RTM)
    parser.add_argument("--tracker", type=Path, default=TRACKER)
    parser.add_argument("--backlog", type=Path, default=BACKLOG)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument(
        "--check",
        action="store_true",
        help="Validate only; do not rewrite --out unless also passing --write.",
    )
    parser.add_argument(
        "--write",
        action="store_true",
        help="Write the coverage matrix JSON to --out.",
    )
    args = parser.parse_args(argv)

    missing = [str(path) for path in (args.sas, args.rtm, args.tracker) if not path.is_file()]
    if missing:
        print(
            json.dumps(
                {
                    "ok": False,
                    "error": "missing_sources",
                    "missing": missing,
                }
            ),
            file=sys.stderr,
        )
        return 2

    backlog_path = args.backlog
    if not backlog_path.is_file():
        # Fall back to the tracker-declared backlog path when present.
        tracker_preview = load_json(args.tracker)
        declared = tracker_preview.get("current_backlog", {}).get("path")
        if declared:
            backlog_path = REPO / declared
    if not backlog_path.is_file():
        print(
            json.dumps(
                {
                    "ok": False,
                    "error": "missing_backlog",
                    "path": str(backlog_path),
                }
            ),
            file=sys.stderr,
        )
        return 2

    matrix = build_matrix(
        args.sas.read_text(encoding="utf-8"),
        args.rtm.read_text(encoding="utf-8"),
        load_json(backlog_path),
        load_json(args.tracker),
    )

    if args.write or not args.check:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(matrix, indent=2) + "\n", encoding="utf-8")

    summary = {
        "ok": matrix["acceptance"]["met"],
        "requirements": matrix["counts"]["requirements"],
        "uncovered_must": matrix["uncovered_must"],
        "untracked_release_gates": matrix["untracked_release_gates"],
        "sas_rtm_id_drift": matrix["sas_rtm_id_drift"],
        "out": str(args.out.relative_to(REPO)) if args.out.is_relative_to(REPO) else str(args.out),
    }
    print(json.dumps(summary, ensure_ascii=False))
    return 0 if matrix["acceptance"]["met"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
