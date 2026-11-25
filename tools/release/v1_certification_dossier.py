#!/usr/bin/env python3
"""Assemble the V1 release certification dossier from committed evidence.

measurement=none: aggregates FR/NFR coverage, B-01..B-05, goldens/security/docs,
and ownership/concurrency/crash/measurement audits into a signed checklist.
Does not create the Git tag (that is a later release step) and does not re-run
performance series.
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parents[2]
TRACKER = REPO / "IMPLEMENTATION_STATUS.json"
COVERAGE = REPO / "contracts/traceability/requirements-coverage-v1.json"
BACKLOG = REPO / "motion_framework_sas/implementation_tasks.json"

# Direct evidence dependencies for the dossier (backlog depends_on).
DOSSIER_DEPS = (
    "P8-023",
    "P8-024",
    "P8-016",
    "P8-029",
    "P8-031",
    "P8-035",
    "P8-038",
    "P8-039",
    "P8-040",
    "P8-041",
)

AUDIT_TASKS = ("P8-038", "P8-039", "P8-040", "P8-041")

CHECKLIST: tuple[dict[str, Any], ...] = (
    {
        "id": "reference_films",
        "title": "Three publishable silent 1080x1920/30 reference MP4s",
        "evidence": [
            "implementation-evidence/P8-035/export-report.json",
            "implementation-evidence/P8-035/reference-films-check.json",
            "contracts/release/reference-films/catalog-v1.json",
        ],
        "task_ids": ["P8-035"],
    },
    {
        "id": "b01_b02_b03_walls",
        "title": "B-01/B-02/B-03 cold export_wall_ms ≤ 360000 with explicit thresholds",
        "evidence": [
            "implementation-evidence/P8-023/certificate.json",
            "implementation-evidence/P8-023/decision.json",
        ],
        "task_ids": ["P8-023"],
    },
    {
        "id": "b04_mixed_wall",
        "title": "B-04 mixed 5 min cold export_wall_ms ≤ 1800000 with locked profile",
        "evidence": [
            "implementation-evidence/P8-021/decision.json",
            "implementation-evidence/P8-021/baseline-summary.json",
        ],
        "task_ids": ["P8-021"],
    },
    {
        "id": "b05_complexity_tier",
        "title": "B-05 worst-accepted complexity tier published without OOM",
        "evidence": [
            "implementation-evidence/P8-022/decision.json",
            "implementation-evidence/P8-022/complexity-tier.json",
        ],
        "task_ids": ["P8-022"],
    },
    {
        "id": "video_conformance",
        "title": "Reference MP4 conformance (PTS, frame count, Rec.709, no audio)",
        "evidence": [
            "implementation-evidence/P8-016/suite-report.json",
            "implementation-evidence/P8-035/reference-films-check.json",
        ],
        "task_ids": ["P8-016", "P8-035"],
    },
    {
        "id": "endurance_no_fatal_growth",
        "title": "B-04/B-05 endurance without fatal slowdown or unbounded working set",
        "evidence": [
            "implementation-evidence/P8-024/decision.json",
            "implementation-evidence/P8-024/suite.json",
        ],
        "task_ids": ["P8-024"],
    },
    {
        "id": "security_and_licenses",
        "title": "Asset security, licenses, and dependency pins without critical findings",
        "evidence": [
            "implementation-evidence/P8-029/security-audit-artifacts/security-audit-report.json",
            "contracts/security-audit/allowed-licenses-v1.json",
        ],
        "task_ids": ["P8-029"],
    },
    {
        "id": "docs_and_tutorials",
        "title": "Clean tutorial install produces all three style videos",
        "evidence": [
            "implementation-evidence/P8-031/tutorial-artifacts/tutorial-e2e-report.json",
            "tutorials/README.md",
        ],
        "task_ids": ["P8-031"],
    },
    {
        "id": "ownership_raii_audit",
        "title": "Ownership/RAII/async lifetime audit PASS",
        "evidence": [
            "implementation-evidence/P8-038/ownership-audit-check.json",
            "implementation-evidence/P8-038/ownership-lifetime-note.md",
        ],
        "task_ids": ["P8-038"],
    },
    {
        "id": "concurrency_backpressure_audit",
        "title": "Concurrency/backpressure/shutdown audit PASS",
        "evidence": [
            "implementation-evidence/P8-039/overload-report.json",
            "implementation-evidence/P8-039/concurrency-note.md",
        ],
        "task_ids": ["P8-039"],
    },
    {
        "id": "crash_durability_audit",
        "title": "Crash consistency and durability class audit PASS",
        "evidence": [
            "implementation-evidence/P8-040/crash-matrix.json",
            "implementation-evidence/P8-040/decision.md",
            "implementation-evidence/P8-040/capability-report.json",
        ],
        "task_ids": ["P8-040"],
    },
    {
        "id": "measurement_reproducibility_audit",
        "title": "Metric reproducibility audit PASS (fixtures/lost samples cannot PASS)",
        "evidence": [
            "implementation-evidence/P8-041/audit-report.json",
            "implementation-evidence/P8-041/decision.md",
        ],
        "task_ids": ["P8-041"],
    },
    {
        "id": "fr_nfr_coverage",
        "title": "SAS FR/NFR coverage matrix has no uncovered MUST",
        "evidence": [
            "contracts/traceability/requirements-coverage-v1.json",
            "implementation-evidence/P8-001/decision.json",
        ],
        "task_ids": ["P8-001"],
    },
)


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def _display_path(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(REPO))
    except ValueError:
        return path.name


def evidence_exists(rel: str) -> bool:
    return (REPO / rel).is_file() or (REPO / rel).is_dir()


def task_done(tracker: dict[str, Any], task_id: str) -> dict[str, Any]:
    entry = (tracker.get("tasks") or {}).get(task_id) or {}
    acceptance = entry.get("acceptance") or {}
    evidence = entry.get("evidence") or []
    checks = entry.get("checks") or []
    status = entry.get("status")
    met = acceptance.get("met") is True
    evidenced = bool(evidence)
    for item in evidence:
        if not evidence_exists(str(item).rstrip("/")):
            evidenced = False
            break
    return {
        "task_id": task_id,
        "status": status,
        "acceptance_met": met,
        "evidenced": evidenced,
        "has_checks": bool(checks),
        "pass": status == "done" and met and evidenced,
    }


def evaluate_phase_gates(tracker: dict[str, Any]) -> dict[str, Any]:
    gates = tracker.get("phase_gates") or {}
    results = {}
    all_prior_pass = True
    for phase in ("P0", "P1", "P2", "P3", "P4", "P5", "P6", "P7"):
        gate = gates.get(phase) or {}
        decision = gate.get("decision")
        status = gate.get("status")
        ok = status == "done" and decision in {"PASS", "GO"}
        results[phase] = {
            "status": status,
            "decision": decision,
            "gate_task_id": gate.get("gate_task_id"),
            "pass": ok,
        }
        all_prior_pass = all_prior_pass and ok
    p8 = gates.get("P8") or {}
    results["P8"] = {
        "status": p8.get("status"),
        "decision": p8.get("decision"),
        "gate_task_id": p8.get("gate_task_id"),
        "pass": False,
        "note": "Phase gate remains open until the verified v1.0.0 tag step",
    }
    return {
        "prior_phases_pass": all_prior_pass,
        "gates": results,
    }


def evaluate_coverage(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {"pass": False, "reason": "coverage_matrix_missing"}
    matrix = load_json(path)
    acceptance = matrix.get("acceptance") or {}
    uncovered = matrix.get("uncovered_must") or []
    return {
        "pass": (
            acceptance.get("no_uncovered_must") is True
            and acceptance.get("met") is True
            and uncovered == []
        ),
        "path": str(path.relative_to(REPO)),
        "counts": matrix.get("counts"),
        "uncovered_must": uncovered,
    }


def evaluate_checklist(
    tracker: dict[str, Any],
) -> list[dict[str, Any]]:
    items: list[dict[str, Any]] = []
    for spec in CHECKLIST:
        missing = [rel for rel in spec["evidence"] if not evidence_exists(rel)]
        task_results = [task_done(tracker, task_id) for task_id in spec["task_ids"]]
        tasks_ok = all(result["pass"] for result in task_results)
        item_pass = not missing and tasks_ok
        items.append(
            {
                "id": spec["id"],
                "title": spec["title"],
                "pass": item_pass,
                "evidence": spec["evidence"],
                "missing_evidence": missing,
                "tasks": task_results,
            }
        )
    return items


def evaluate_audits(tracker: dict[str, Any]) -> dict[str, Any]:
    results = {task_id: task_done(tracker, task_id) for task_id in AUDIT_TASKS}
    return {
        "pass": all(item["pass"] for item in results.values()),
        "tasks": results,
    }


def evaluate_deps(tracker: dict[str, Any]) -> dict[str, Any]:
    results = {task_id: task_done(tracker, task_id) for task_id in DOSSIER_DEPS}
    return {
        "pass": all(item["pass"] for item in results.values()),
        "tasks": results,
    }


def known_limitations() -> list[str]:
    return [
        "Individual performance run reports remain certified=false; series certificates are threshold gates, not a self-certifying single run.",
        "SIGKILL / process-crash tests are not power-loss durability evidence.",
        "Telemetry overhead experiments are not performance PASS and are never subtracted from official results.",
        "The P8 phase gate and Git tag v1.0.0 remain a separate release step after this dossier.",
        "Sanitized or non-reference hardware timings are not M1 Air certification claims.",
    ]


def assemble_dossier(
    *,
    tracker_path: Path = TRACKER,
    coverage_path: Path = COVERAGE,
) -> dict[str, Any]:
    tracker = load_json(tracker_path)
    deps = evaluate_deps(tracker)
    gates = evaluate_phase_gates(tracker)
    coverage = evaluate_coverage(coverage_path)
    checklist = evaluate_checklist(tracker)
    audits = evaluate_audits(tracker)

    blocking_defects: list[str] = []
    if not deps["pass"]:
        blocking_defects.append("dossier_dependency_incomplete")
    if not gates["prior_phases_pass"]:
        blocking_defects.append("prior_phase_gate_incomplete")
    if not coverage["pass"]:
        blocking_defects.append("fr_nfr_coverage_incomplete")
    if not audits["pass"]:
        blocking_defects.append("runtime_audit_incomplete")
    failed_items = [item["id"] for item in checklist if not item["pass"]]
    if failed_items:
        blocking_defects.append("checklist_items_failed:" + ",".join(failed_items))

    unexplained_missing_metrics = False
    metric_audit = REPO / "implementation-evidence/P8-041/audit-report.json"
    if metric_audit.is_file():
        report = load_json(metric_audit)
        if report.get("pass") is not True:
            unexplained_missing_metrics = True
            blocking_defects.append("measurement_audit_failed")
    else:
        unexplained_missing_metrics = True
        blocking_defects.append("measurement_audit_missing")

    accepted = (
        not blocking_defects
        and deps["pass"]
        and gates["prior_phases_pass"]
        and coverage["pass"]
        and audits["pass"]
        and all(item["pass"] for item in checklist)
        and not unexplained_missing_metrics
    )

    return {
        "schema_version": 1,
        "kind": "v1_release_certification_dossier",
        "generated_at_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "measurement": "none",
        "certified_performance": False,
        "product_v1_certificate": accepted,
        "git_tag_created": False,
        "decision": "accept_v1_release_certification_dossier" if accepted else "reject",
        "accepted": accepted,
        "blocking_defects": blocking_defects,
        "unexplained_missing_metrics": unexplained_missing_metrics,
        "dependencies": deps,
        "phase_gates": gates,
        "fr_nfr_coverage": coverage,
        "runtime_audits": audits,
        "checklist": checklist,
        "known_limitations": known_limitations(),
        "sources": {
            "tracker": _display_path(tracker_path),
            "coverage": _display_path(coverage_path),
            "milestones": "motion_framework_sas/MILESTONES_AND_RELEASE_GATES.md",
        },
        "notes": [
            "This dossier aggregates committed evidence; it does not re-measure B-0x series.",
            "Tagging v1.0.0 remains a subsequent release action after dossier acceptance.",
        ],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--report",
        type=Path,
        help="write the dossier JSON to this path",
    )
    parser.add_argument(
        "--checklist",
        type=Path,
        help="write the signed checklist JSON to this path",
    )
    args = parser.parse_args(argv)
    dossier = assemble_dossier()
    checklist_doc = {
        "schema_version": 1,
        "kind": "v1_release_checklist",
        "generated_at_utc": dossier["generated_at_utc"],
        "pass": dossier["accepted"],
        "signed": dossier["accepted"],
        "product_v1_certificate": dossier["product_v1_certificate"],
        "git_tag_created": False,
        "items": [
            {
                "id": item["id"],
                "title": item["title"],
                "pass": item["pass"],
                "evidence": item["evidence"],
            }
            for item in dossier["checklist"]
        ],
        "blocking_defects": dossier["blocking_defects"],
        "known_limitations": dossier["known_limitations"],
    }

    text = json.dumps(dossier, indent=2) + "\n"
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text, encoding="utf-8")
    if args.checklist is not None:
        args.checklist.parent.mkdir(parents=True, exist_ok=True)
        args.checklist.write_text(
            json.dumps(checklist_doc, indent=2) + "\n", encoding="utf-8"
        )
    if args.report is None and args.checklist is None:
        sys.stdout.write(text)
    else:
        sys.stdout.write(
            json.dumps(
                {
                    "accepted": dossier["accepted"],
                    "decision": dossier["decision"],
                    "blocking_defects": dossier["blocking_defects"],
                }
            )
            + "\n"
        )
    return 0 if dossier["accepted"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
