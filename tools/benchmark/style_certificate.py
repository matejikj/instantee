#!/usr/bin/env python3
"""Assemble the B-01/B-02/B-03 60 s style certificate from locked reference series.

Individual run reports remain certified=false. This certificate records the
shared threshold gate: every valid cold trial ≤360 s with explicit non-null
p99, memory, and CPU ceilings. Null/absent thresholds FAIL.
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]

STYLE_SPECS: dict[str, dict[str, Any]] = {
    "B-01": {
        "workload_id": "B-01-editorial-news-reel-60s-v1",
        "evidence_kind": "b01_60s_cold_warm_reference_series",
        "quality_tier": "editorial-b01-v1",
        "frames": 1800,
        "export_wall_ms_max": 360_000,
    },
    "B-02": {
        "workload_id": "B-02-notebook-pythagoras-explainer-60s-v1",
        "evidence_kind": "b02_60s_cold_warm_reference_series",
        "quality_tier": "notebook-b02-v1",
        "frames": 1800,
        "export_wall_ms_max": 360_000,
    },
    "B-03": {
        "workload_id": "B-03-paper-historical-reel-60s-v1",
        "evidence_kind": "b03_60s_cold_warm_reference_series",
        "quality_tier": "paper-b03-v1",
        "frames": 1800,
        "export_wall_ms_max": 360_000,
    },
}

REQUIRED_THRESHOLD_KEYS = (
    "export_wall_ms_max",
    "frame_latency_p99_ns_max",
    "rss_peak_bytes_max",
    "cpu_seconds_per_verified_frame_max",
)


def thresholds_explicit(thresholds: Any) -> tuple[bool, str]:
    """Unknown/null thresholds are an automatic FAIL for the style certificate."""
    if not isinstance(thresholds, dict):
        return False, "thresholds_missing"
    if any(thresholds.get(key) is None for key in REQUIRED_THRESHOLD_KEYS):
        return False, "threshold_null"
    if any(key not in thresholds for key in REQUIRED_THRESHOLD_KEYS):
        return False, "threshold_incomplete"
    return True, "explicit"


def _median(values: list[float]) -> float | None:
    return statistics.median(values) if values else None


def validate_style_series(style: str, baseline: dict[str, Any]) -> dict[str, Any]:
    spec = STYLE_SPECS[style]
    reasons: list[str] = []

    if baseline.get("certified") is not False:
        reasons.append("run_series_must_stay_uncertified")
    if baseline.get("evidence_kind") != spec["evidence_kind"]:
        reasons.append("evidence_kind_mismatch")
    if baseline.get("workload_id") != spec["workload_id"]:
        reasons.append("workload_id_mismatch")
    if baseline.get("frames_per_trial") != spec["frames"]:
        reasons.append("frames_mismatch")

    thresholds = baseline.get("thresholds")
    explicit, explicit_reason = thresholds_explicit(thresholds)
    if not explicit:
        reasons.append(explicit_reason)
    elif thresholds["export_wall_ms_max"] > spec["export_wall_ms_max"]:
        # Profile may be stricter than the protocol hard wall, never looser.
        reasons.append("wall_ceiling_above_protocol")
    elif thresholds["export_wall_ms_max"] != spec["export_wall_ms_max"]:
        # Allow equal protocol wall only for the shared 60 s hard target.
        pass

    runs = baseline.get("runs")
    if not isinstance(runs, list) or not runs:
        reasons.append("runs_missing")
        runs = []

    cold = [run for run in runs if run.get("cache_mode") == "cold"]
    warm = [run for run in runs if run.get("cache_mode") == "warm"]
    if len(cold) < 3:
        reasons.append("cold_repeats_below_3")
    if len(warm) < 3:
        reasons.append("warm_repeats_below_3")

    cold_complete = all(run.get("measurement_complete") for run in cold) and bool(cold)
    warm_complete = all(run.get("measurement_complete") for run in warm) and bool(warm)
    if not cold_complete:
        reasons.append("cold_measurement_incomplete")
    if not warm_complete:
        reasons.append("warm_measurement_incomplete")

    wall_max = (
        thresholds.get("export_wall_ms_max")
        if isinstance(thresholds, dict)
        else None
    )
    cold_wall_ok = False
    if cold_complete and isinstance(wall_max, (int, float)):
        cold_wall_ok = all(
            isinstance(run.get("export_wall_ms"), (int, float))
            and run["export_wall_ms"] <= wall_max
            and run["export_wall_ms"] <= spec["export_wall_ms_max"]
            for run in cold
        )
        if not cold_wall_ok:
            reasons.append("cold_wall_exceeded")
    elif cold_complete:
        reasons.append("cold_wall_uncheckable")

    if baseline.get("series_pass") is not True:
        reasons.append("series_pass_false")
    if baseline.get("cold_wall_within_360s") is not True:
        reasons.append("cold_wall_within_360s_false")

    identity_ok = (
        baseline.get("certified") is False
        and baseline.get("evidence_kind") == spec["evidence_kind"]
        and baseline.get("workload_id") == spec["workload_id"]
        and baseline.get("frames_per_trial") == spec["frames"]
    )
    series_pass = (
        identity_ok
        and explicit
        and cold_complete
        and warm_complete
        and cold_wall_ok
        and len(cold) >= 3
        and len(warm) >= 3
        and baseline.get("series_pass") is True
        and baseline.get("cold_wall_within_360s") is True
        and "wall_ceiling_above_protocol" not in reasons
        and "runs_missing" not in reasons
    )

    cold_walls = [
        float(run["export_wall_ms"])
        for run in cold
        if isinstance(run.get("export_wall_ms"), (int, float))
    ]
    cold_p99 = [
        float(run["frame_latency_p99_ns"])
        for run in cold
        if isinstance(run.get("frame_latency_p99_ns"), (int, float))
    ]
    cold_rss = [
        float(run["rss_peak_bytes"])
        for run in cold
        if isinstance(run.get("rss_peak_bytes"), (int, float))
    ]
    cold_cpu = [
        float(run["cpu_seconds_per_verified_frame"])
        for run in cold
        if isinstance(run.get("cpu_seconds_per_verified_frame"), (int, float))
    ]

    return {
        "style": style,
        "workload_id": baseline.get("workload_id"),
        "quality_tier": spec["quality_tier"],
        "pass": series_pass,
        "reasons": reasons,
        "thresholds_explicit": explicit,
        "thresholds_reason": explicit_reason,
        "thresholds": thresholds if isinstance(thresholds, dict) else None,
        "cold_repeats": len(cold),
        "warm_repeats": len(warm),
        "cold_wall_within_360s": cold_wall_ok,
        "series_pass": baseline.get("series_pass"),
        "profile_sha256": baseline.get("profile_sha256"),
        "summary": {
            "cold_median_export_wall_ms": _median(cold_walls),
            "cold_median_p99_ns": _median(cold_p99),
            "cold_median_rss_peak_bytes": _median(cold_rss),
            "cold_median_cpu_seconds_per_verified_frame": _median(cold_cpu),
            "cold_min_export_wall_ms": min(cold_walls) if cold_walls else None,
            "cold_max_export_wall_ms": max(cold_walls) if cold_walls else None,
        },
        "runs": [
            {
                "run_id": run.get("run_id"),
                "cache_mode": run.get("cache_mode"),
                "pass": run.get("pass"),
                "measurement_complete": run.get("measurement_complete"),
                "export_wall_ms": run.get("export_wall_ms"),
                "frame_latency_p99_ns": run.get("frame_latency_p99_ns"),
                "rss_peak_bytes": run.get("rss_peak_bytes"),
                "cpu_seconds_per_verified_frame": run.get(
                    "cpu_seconds_per_verified_frame"
                ),
                "threshold_reason": run.get("threshold_reason"),
            }
            for run in runs
        ],
    }


def assemble_certificate(
    baselines: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    """Build the B-01/B-02/B-03 certificate from three baseline documents."""
    missing = [style for style in STYLE_SPECS if style not in baselines]
    if missing:
        raise ValueError(f"missing style baselines: {','.join(missing)}")

    styles = {
        style: validate_style_series(style, baselines[style]) for style in STYLE_SPECS
    }
    gate = all(entry["pass"] for entry in styles.values())
    return {
        "evidence_kind": "b01_b02_b03_60s_style_certificate",
        "certificate_kind": "style_reference_series_threshold_gate",
        "pass": gate,
        "run_reports_certified": False,
        "product_v1_certificate": False,
        "protocol_hard_wall_ms": 360_000,
        "required_repeats": {"cold": 3, "warm": 3},
        "policy": {
            "null_thresholds": "FAIL",
            "best_run_selection": False,
            "median_role": "summary_only",
            "every_valid_cold_must_meet_wall": True,
            "cpu_baseline": "explicit_seconds_per_verified_frame_ceiling",
        },
        "styles": styles,
        "decision": {
            "threshold_gate": gate,
            "notes": [
                "Every valid cold trial must meet export_wall_ms ≤ 360000",
                "p99, RSS, and CPU ceilings must be explicit non-null values",
                "Individual run reports remain certified=false",
                "This certificate is the B-01/B-02/B-03 threshold gate, not the V1 product dossier",
                "Allowlisted machine/OS facts only; no owner or device identity",
            ],
        },
        "notes": [
            "All trials retained across styles; no filtering of slow runs",
            "Fresh series required after renderer/preview changes that affect export cost",
        ],
    }


def load_baseline(path: Path) -> dict[str, Any]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError(f"{path}: baseline must be an object")
    return payload


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--b01", type=Path, required=True, help="B-01 baseline.json")
    parser.add_argument("--b02", type=Path, required=True, help="B-02 baseline.json")
    parser.add_argument("--b03", type=Path, required=True, help="B-03 baseline.json")
    parser.add_argument("--out", type=Path, required=True, help="certificate JSON path")
    args = parser.parse_args(argv)

    certificate = assemble_certificate(
        {
            "B-01": load_baseline(args.b01),
            "B-02": load_baseline(args.b02),
            "B-03": load_baseline(args.b03),
        }
    )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(certificate, indent=2) + "\n", encoding="utf-8")
    print(args.out)
    return 0 if certificate["pass"] else 6


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(
            json.dumps(
                {
                    "code": "style_certificate_failed",
                    "phase": "harness",
                    "error": str(exc),
                    "published": None,
                    "durability": "unknown",
                }
            ),
            file=sys.stderr,
        )
        raise SystemExit(6) from exc
