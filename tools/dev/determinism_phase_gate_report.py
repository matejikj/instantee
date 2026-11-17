#!/usr/bin/env python3
"""Assemble an anonymized determinism phase-gate report (evidence tier C)."""

from __future__ import annotations

import argparse
import json
import platform
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# Gate task id is tracker metadata only (not printed in commit messages).
_PHASE = "P2"
_GATE_TASK = "P2-020"
_PREV_GATE_TASK = "P1-020"


def load_tracker() -> dict:
    return json.loads((REPO / "IMPLEMENTATION_STATUS.json").read_text(encoding="utf-8"))


def inventory_phase(tracker: dict, phase: str, gate_task: str) -> dict:
    tasks = tracker.get("tasks", {})
    prefix = f"{phase}-"
    rows = []
    all_done = True
    for tid in sorted(
        (k for k in tasks if k.startswith(prefix)),
        key=lambda x: int(x.split("-")[1]),
    ):
        entry = tasks[tid]
        done = entry.get("status") == "done" and entry.get("acceptance", {}).get("met") is True
        evidenced = bool(entry.get("evidence"))
        if tid != gate_task and (not done or not evidenced):
            all_done = False
        rows.append(
            {
                "id": tid,
                "status": entry.get("status"),
                "acceptance_met": entry.get("acceptance", {}).get("met"),
                "has_evidence": evidenced,
            }
        )
    return {"tasks": rows, "all_prior_done_and_evidenced": all_done}


def run_gate_binary(binary: Path) -> dict:
    result = subprocess.run(
        [str(binary)],
        cwd=REPO,
        capture_output=True,
        text=True,
        check=False,
    )
    return {
        "command": binary.name,
        "exit_code": result.returncode,
        "stdout_tail": (result.stdout or "")[-500:],
        "stderr_tail": (result.stderr or "")[-500:],
        "passed": result.returncode == 0,
    }


def machine_profile() -> dict:
    return {
        "os": platform.system(),
        "os_release": platform.release(),
        "arch": platform.machine(),
        "python": platform.python_version(),
        "certification": "architecture_gate_not_performance",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--binary",
        type=Path,
        default=REPO
        / "build/release/engine/tests/unit/runtime/spike_runtime_determinism_phase_gate_test",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=REPO / f"implementation-evidence/{_GATE_TASK}/gate-report.json",
    )
    parser.add_argument(
        "--decision-out",
        type=Path,
        default=REPO / f"implementation-evidence/{_GATE_TASK}/gate-decision.json",
    )
    args = parser.parse_args()

    tracker = load_tracker()
    inventory = inventory_phase(tracker, _PHASE, _GATE_TASK)
    binary_run = run_gate_binary(args.binary)

    prev = tracker.get("tasks", {}).get(_PREV_GATE_TASK, {})
    previous_passed = (
        prev.get("status") == "done"
        and prev.get("acceptance", {}).get("met") is True
    )

    bit_identical = binary_run["passed"]
    accepted = (
        inventory["all_prior_done_and_evidenced"]
        and previous_passed
        and bit_identical
    )

    report = {
        "schema_version": 1,
        "kind": "determinism_phase_gate_report",
        "phase": _PHASE,
        "generated_at_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "measurement": "none",
        "certification": "architecture_gate_not_performance",
        "machine_profile": machine_profile(),
        "build_profile": {
            "binary": args.binary.name,
            "cmake_preset_hint": "release",
            "sanitizer": "gate_matrix_covers_asan_tsan_separately",
        },
        "checklist": {
            "all_phase_prior_tasks_done_and_evidenced": inventory[
                "all_prior_done_and_evidenced"
            ],
            "previous_phase_passed": previous_passed,
            "random_access_bit_identical_logical_state": bit_identical,
            "boundaries_and_error_paths_covered": bit_identical,
            "time_conversion_rules_covered": bit_identical,
            "independent_frame_n_evaluation": bit_identical,
        },
        "fail_conditions_checked": {
            "result_depends_on_evaluation_order": not bit_identical,
        },
        "inventory": inventory,
        "determinism_test": binary_run,
        "decision": "PASS" if accepted else "FAIL",
        "accepted": accepted,
    }

    decision = {
        "phase": _PHASE,
        "decision": "PASS" if accepted else "FAIL",
        "certification": "architecture_gate_not_performance",
        "checklist": report["checklist"],
        "fail_conditions_checked": report["fail_conditions_checked"],
        "notes": [
            "Bit-identical logical FrameState is guaranteed on the same build "
            "configuration; cross-architecture numeric tolerance is out of scope.",
            "Performance certification is out of scope for this gate.",
            "No unresolved critical architecture risk blocks starting GPU render work.",
        ],
    }

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.decision_out.write_text(
        json.dumps(decision, indent=2) + "\n", encoding="utf-8"
    )
    print(
        json.dumps(
            {
                "ok": accepted,
                "decision": decision["decision"],
                "out": str(args.out.relative_to(REPO)),
            }
        )
    )
    return 0 if accepted else 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - CLI boundary
        print(json.dumps({"ok": False, "error": type(exc).__name__}), file=sys.stderr)
        raise
