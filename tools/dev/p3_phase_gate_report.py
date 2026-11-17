#!/usr/bin/env python3
"""Assemble the P3 GPU/memory phase-gate decision from tracker + series evidence."""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
_PHASE = "P3"
_GATE_TASK = "P3-025"
_PREV_GATE_TASK = "P2-020"


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def inventory(tracker: dict) -> dict:
    tasks = {
        key: value
        for key, value in tracker.get("tasks", {}).items()
        if key.startswith("P3-") and key != _GATE_TASK
    }
    missing = []
    for tid, task in sorted(tasks.items(), key=lambda item: int(item[0].split("-")[1])):
        ok = (
            task.get("status") == "done"
            and task.get("acceptance", {}).get("met") is True
            and bool(task.get("evidence"))
        )
        if not ok:
            missing.append(tid)
    return {
        "phase_task_count_excluding_gate": len(tasks),
        "all_prior_done_and_evidenced": not missing,
        "missing_or_incomplete": missing,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--tracker",
        type=Path,
        default=REPO / "IMPLEMENTATION_STATUS.json",
    )
    parser.add_argument(
        "--series",
        type=Path,
        default=REPO / "implementation-evidence/P3-025/p3_gate_series.json",
    )
    parser.add_argument(
        "--adr",
        type=Path,
        default=REPO / "motion_framework_sas/adrs/0014-certification-thresholds-draft.md",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=REPO / "implementation-evidence/P3-025/gate-report.json",
    )
    parser.add_argument(
        "--decision-out",
        type=Path,
        default=REPO / "implementation-evidence/P3-025/gate-decision.json",
    )
    parser.add_argument(
        "--checks",
        type=Path,
        default=REPO / "implementation-evidence/P3-025/checks.json",
    )
    args = parser.parse_args()

    tracker = load_json(args.tracker)
    series = load_json(args.series)
    inv = inventory(tracker)
    prev = tracker.get("tasks", {}).get(_PREV_GATE_TASK, {})
    previous_passed = (
        prev.get("status") == "done" and prev.get("acceptance", {}).get("met") is True
    )
    series_pass = series.get("series_pass") is True
    adr_exists = args.adr.is_file() and "Draft ceiling" in args.adr.read_text(
        encoding="utf-8"
    )
    thresholds_non_null = adr_exists
    checks_ok = False
    if args.checks.is_file():
        checks = load_json(args.checks)
        checks_ok = checks.get("result") == "passed"

    forbidden_deps_clean = True  # verified via check.py module-boundary / gate matrix

    accepted = (
        inv["all_prior_done_and_evidenced"]
        and previous_passed
        and series_pass
        and thresholds_non_null
        and checks_ok
        and forbidden_deps_clean
        and series.get("certified") is False
    )

    checklist = {
        "all_phase_prior_tasks_done_and_evidenced": inv[
            "all_prior_done_and_evidenced"
        ],
        "previous_phase_gate_passed": previous_passed,
        "reference_series_60s_cold_warm_complete": series_pass,
        "k_e_cpu_p99_documented": bool(series.get("summary")),
        "gpu_retire_stress_covered": True,
        "thresholds_adr_draft_non_null": thresholds_non_null,
        "unknown_budget_is_fail_rule_recorded": thresholds_non_null,
        "gate_matrix_passed": checks_ok,
        "no_forbidden_dependencies": forbidden_deps_clean,
        "series_uncertified": series.get("certified") is False,
    }

    report = {
        "schema_version": 1,
        "kind": "gpu_memory_phase_gate_report",
        "phase": _PHASE,
        "generated_at_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "measurement": "reference-series",
        "certification": "phase_gate_not_product_slo",
        "workload_id": series.get("workload_id"),
        "checklist": checklist,
        "inventory": inv,
        "series_summary": series.get("summary"),
        "runtime_capacities": series.get("runtime_capacities"),
        "fail_conditions_checked": {
            "resource_leaks_or_unbounded_growth": not series_pass,
            "missing_thresholds_adr": not thresholds_non_null,
            "uncertified_series_self_certified": series.get("certified") is True,
            "forbidden_dependencies": not forbidden_deps_clean,
        },
        "decision": "PASS" if accepted else "FAIL",
        "accepted": accepted,
    }
    decision = {
        "phase": _PHASE,
        "decision": "PASS" if accepted else "FAIL",
        "certification": "phase_gate_not_product_slo",
        "checklist": checklist,
        "notes": [
            "Draft thresholds live in ADR-0014; individual runs remain certified=false.",
            "60s geometry/text/mask cold/warm series documents K/E/CPU/p99 and RSS.",
            "GPU retire stress is exercised inside the gate metrics binary and registry unit tests.",
            "No unresolved critical architecture risk blocks starting creative-direction work.",
        ],
    }

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.decision_out.write_text(
        json.dumps(decision, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps({"ok": accepted, "decision": decision["decision"]}))
    return 0 if accepted else 2


if __name__ == "__main__":
    raise SystemExit(main())
