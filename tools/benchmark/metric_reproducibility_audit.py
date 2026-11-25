#!/usr/bin/env python3
"""Independent audit that raw metric evidence matches reports without re-measuring.

measurement=none: recomputes oracles and pinned series summaries from committed
artifacts. Does not spawn exports or claim certified M1 Air performance.
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path
from typing import Any

from measure import cpu_metrics, derive_metrics, nearest_rank
from performance_report import (
    collect_semantic_issues,
    is_certification_eligible,
    load_json,
    validate_performance_report,
)
from style_certificate import STYLE_SPECS, validate_style_series

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "contracts" / "fixtures" / "performance-report"
DEFAULT_SERIES = {
    "B-01": ROOT / "implementation-evidence/P8-023/b01-baseline-summary.json",
    "B-02": ROOT / "implementation-evidence/P8-023/b02-baseline-summary.json",
    "B-03": ROOT / "implementation-evidence/P8-023/b03-baseline-summary.json",
}
DEFAULT_OVERHEAD = ROOT / "implementation-evidence/P0-016/overhead/overhead.json"


def overflow_safe_nearest_rank(values: list[int]) -> dict[str, Any] | None:
    """Match the C++ FrameSamples quantile formula that avoids N*p overflow."""
    if not values:
        return None
    ordered = sorted(values)
    count = len(ordered)

    def quantile(percent: int) -> int:
        rank = (count // 100) * percent + ((count % 100) * percent + 99) // 100
        return ordered[rank - 1]

    return {
        "sample_count": count,
        "min": ordered[0],
        "p50": quantile(50),
        "p95": quantile(95),
        "p99": quantile(99),
        "max": ordered[-1],
        "method": "exact_nearest_rank",
        "interval": "request_before_admission_to_sink_accepted",
        "unit": "ns",
    }


def audit_oracle_agreement() -> dict[str, Any]:
    """Known series must agree across Python harness and overflow-safe formula."""
    series = list(range(1, 101))
    python_rank = nearest_rank(series)
    safe_rank = overflow_safe_nearest_rank(series)
    assert python_rank is not None and safe_rank is not None
    expected = {"min": 1, "p50": 50, "p95": 95, "p99": 99, "max": 100}
    keys = ("min", "p50", "p95", "p99", "max")
    python_ok = {k: python_rank[k] for k in keys} == expected
    safe_ok = {k: safe_rank[k] for k in keys} == expected
    # Large N where naive count*percent overflows uint32 but not Python int.
    large = list(range(1, 10_001))
    large_python = nearest_rank(large)
    large_safe = overflow_safe_nearest_rank(large)
    large_agree = large_python is not None and large_safe is not None and all(
        large_python[k] == large_safe[k] for k in keys
    )
    cpu = cpu_metrics(3_000_000_000, 1_000_000_000, 2_000_000_000, 8, 10)
    cpu_ok = (
        cpu["core_equivalents"] == 2
        and cpu["percent_one_core"] == 200
        and cpu["percent_machine"] == 25
        and cpu["seconds_per_verified_frame"] == 0.4
        and cpu["scope"] == "renderer_process_all_threads"
    )
    return {
        "pass": python_ok and safe_ok and large_agree and cpu_ok,
        "python_matches_known_oracle": python_ok,
        "overflow_safe_matches_known_oracle": safe_ok,
        "large_n_python_vs_overflow_safe": large_agree,
        "cpu_normalization_known_counters": cpu_ok,
        "units": {
            "latency": "ns",
            "method": "exact_nearest_rank",
            "interval": "request_before_admission_to_sink_accepted",
            "cpu_scope": "renderer_process_all_threads",
            "memory": "bytes",
        },
    }


def audit_completeness_and_loss() -> dict[str, Any]:
    """Lost samples, incomplete frames, and counter lies never complete."""
    samples = [
        [i, i * 1000, i * 1000 + 200, i * 1000 + 201, i * 1000 + 202,
         i * 1000 + 203, i * 1000 + 204, i * 1000 + 205, i * 1000 + 900]
        for i in range(3)
    ]
    child = {
        "telemetry_enabled": True,
        "frame_lost_samples": 0,
        "resource_lost_samples": 0,
        "latency_summary": [3, 900, 900, 900, 900, 900],
        "published": True,
        "scheduled": 3,
        "admitted": 3,
        "accepted": 3,
        "verified": 3,
        "in_flight_peak": 2,
        "queue_peak": 1,
        "encoder_pool_peak": 8,
        "finalize_ms": 2.0,
    }
    resources = [[0, 10, 5, 1024, 2048, 512], [100, 20, 10, 2048, 2048, 1024]]
    outer = {
        "exit_code": 0,
        "timed_out": False,
        "wall_ns": 2_000_000_000,
        "user_ns": 3_000_000_000,
        "system_ns": 1_000_000_000,
        "rss_peak_bytes": 4096,
    }
    ok_metrics, ok_complete = derive_metrics(
        dict(child), samples, resources, outer, 3, 8
    )
    lost_child = dict(child, frame_lost_samples=1)
    lost_metrics, lost_complete = derive_metrics(
        lost_child, samples, resources, outer, 3, 8
    )
    truncated_metrics, missing_complete = derive_metrics(
        dict(child), samples[:-1], resources, outer, 3, 8
    )
    return {
        "pass": (
            ok_complete
            and ok_metrics is not None
            and ok_metrics["frame_latency_ns"]["p99"] == 900
            and not lost_complete
            and lost_metrics["effective_fps"] is None
            and not missing_complete
            and truncated_metrics["effective_fps"] is None
        ),
        "complete_oracle_ok": ok_complete,
        "lost_samples_invalidate": not lost_complete,
        "missing_samples_invalidate": not missing_complete,
    }


def audit_report_pass_gates() -> dict[str, Any]:
    """Design fixtures, lost samples, and missing metrics cannot receive PASS."""
    fixture = load_json(FIXTURES / "positive/schema-fixture.json")
    measured = load_json(FIXTURES / "positive/measured-success-min.json")
    validate_performance_report(measured)

    fixture_eligible = is_certification_eligible(fixture)
    measured_eligible = is_certification_eligible(measured)

    lost = json.loads(json.dumps(measured))
    lost["metrics"]["lost_samples"] = 1
    lost_issues = collect_semantic_issues(lost)
    lost_eligible = is_certification_eligible(lost)

    missing = json.loads(json.dumps(measured))
    del missing["metrics"]["frame_latency_ns"]
    missing_issues = collect_semantic_issues(missing)
    missing_eligible = is_certification_eligible(missing)

    claimed_fixture = json.loads(json.dumps(fixture))
    claimed_fixture["evidence_kind"] = "measured"
    claimed_fixture["result"] = "success"
    claimed_eligible = is_certification_eligible(claimed_fixture)

    semantic_negatives = sorted((FIXTURES / "semantic-negative").glob("*.json"))
    negative_results = []
    for path in semantic_negatives:
        report = load_json(path)
        negative_results.append(
            {
                "fixture": path.name,
                "certification_eligible": is_certification_eligible(report),
                "issue_count": len(collect_semantic_issues(report)),
            }
        )

    profiles_explicit = (
        isinstance(measured.get("machine_profile"), dict)
        and isinstance(measured.get("build_profile"), dict)
        and measured["metrics"]["frame_latency_ns"]["unit"] == "ns"
        and measured["metrics"]["frame_latency_ns"]["method"] == "exact_nearest_rank"
        and measured["metrics"]["frame_latency_ns"]["interval"]
        == "request_before_admission_to_sink_accepted"
        and measured["metrics"]["cpu"]["scope"] == "renderer_process_all_threads"
        and measured["metrics"]["memory"]["unit"] == "bytes"
    )

    return {
        "pass": (
            not fixture_eligible
            and measured_eligible
            and not lost_eligible
            and bool(lost_issues)
            and not missing_eligible
            and bool(missing_issues)
            and not claimed_eligible
            and profiles_explicit
            and all(not item["certification_eligible"] for item in negative_results)
            and len(negative_results) >= 6
        ),
        "schema_fixture_not_eligible": not fixture_eligible,
        "measured_success_eligible_uncertified": measured_eligible
        and measured.get("certified") is False,
        "lost_samples_not_eligible": not lost_eligible,
        "missing_metrics_not_eligible": not missing_eligible,
        "mutated_fixture_not_eligible": not claimed_eligible,
        "units_intervals_profiles_explicit": profiles_explicit,
        "semantic_negatives": negative_results,
    }


def _approx_equal(left: float, right: float, rel: float = 1e-12) -> bool:
    if left == right:
        return True
    scale = max(abs(left), abs(right), 1.0)
    return abs(left - right) <= rel * scale


def audit_independent_series(
    series_paths: dict[str, Path],
) -> dict[str, Any]:
    """Recompute cold/warm medians from pinned runs; no fresh measurement."""
    styles: dict[str, Any] = {}
    all_pass = True
    for style, path in series_paths.items():
        baseline = load_json(path)
        validated = validate_style_series(style, baseline)
        cold = [run for run in baseline.get("runs", []) if run.get("cache_mode") == "cold"]
        warm = [run for run in baseline.get("runs", []) if run.get("cache_mode") == "warm"]
        recomputed = {
            "cold_median_export_wall_ms": statistics.median(
                [float(run["export_wall_ms"]) for run in cold]
            ),
            "warm_median_export_wall_ms": statistics.median(
                [float(run["export_wall_ms"]) for run in warm]
            ),
            "cold_median_p99_ns": statistics.median(
                [float(run["frame_latency_p99_ns"]) for run in cold]
            ),
            "warm_median_p99_ns": statistics.median(
                [float(run["frame_latency_p99_ns"]) for run in warm]
            ),
            "cold_median_rss_peak_bytes": statistics.median(
                [float(run["rss_peak_bytes"]) for run in cold]
            ),
            "warm_median_rss_peak_bytes": statistics.median(
                [float(run["rss_peak_bytes"]) for run in warm]
            ),
            "cold_median_cpu_seconds_per_verified_frame": statistics.median(
                [float(run["cpu_seconds_per_verified_frame"]) for run in cold]
            ),
            "warm_median_cpu_seconds_per_verified_frame": statistics.median(
                [float(run["cpu_seconds_per_verified_frame"]) for run in warm]
            ),
        }
        reported = baseline.get("summary") or {}
        summary_matches = all(
            key in reported and _approx_equal(float(reported[key]), float(value))
            for key, value in recomputed.items()
        )
        thresholds = baseline.get("thresholds") or {}
        thresholds_explicit = all(
            thresholds.get(key) is not None
            for key in (
                "export_wall_ms_max",
                "frame_latency_p99_ns_max",
                "rss_peak_bytes_max",
                "cpu_seconds_per_verified_frame_max",
            )
        )
        style_pass = (
            validated["pass"]
            and summary_matches
            and thresholds_explicit
            and baseline.get("certified") is False
            and len(cold) >= 3
            and len(warm) >= 3
            and all(run.get("measurement_complete") for run in cold + warm)
        )
        all_pass = all_pass and style_pass
        try:
            path_display = str(path.resolve().relative_to(ROOT))
        except ValueError:
            path_display = path.name
        styles[style] = {
            "pass": style_pass,
            "path": path_display,
            "workload_id": baseline.get("workload_id"),
            "expected_workload_id": STYLE_SPECS[style]["workload_id"],
            "cold_repeats": len(cold),
            "warm_repeats": len(warm),
            "summary_matches_independent_recompute": summary_matches,
            "thresholds_explicit": thresholds_explicit,
            "series_validator_pass": validated["pass"],
            "run_reports_remain_uncertified": baseline.get("certified") is False,
            "recomputed_summary": recomputed,
        }
    return {
        "pass": all_pass,
        "measurement": "none",
        "note": (
            "Independent reproduction recomputes medians from committed run "
            "rows; it does not re-run cold/warm exports."
        ),
        "styles": styles,
    }


def audit_telemetry_overhead(path: Path) -> dict[str, Any]:
    overhead = load_json(path)
    pairs = overhead.get("pairs") or []
    return {
        "pass": (
            overhead.get("evidence_kind") == "telemetry_overhead_experiment"
            and overhead.get("certified") is False
            and len(pairs) >= 3
            and overhead.get("cpu_scope") == "renderer_process_all_threads"
        ),
        "path": str(path.relative_to(ROOT)),
        "certified": overhead.get("certified"),
        "pair_count": len(pairs),
        "not_performance_pass": overhead.get("certified") is False,
    }


def run_audit(
    *,
    series_paths: dict[str, Path] | None = None,
    overhead_path: Path | None = None,
) -> dict[str, Any]:
    oracle = audit_oracle_agreement()
    completeness = audit_completeness_and_loss()
    reports = audit_report_pass_gates()
    series = audit_independent_series(series_paths or DEFAULT_SERIES)
    overhead = audit_telemetry_overhead(overhead_path or DEFAULT_OVERHEAD)
    overall = all(
        section["pass"]
        for section in (oracle, completeness, reports, series, overhead)
    )
    return {
        "schema_version": 1,
        "subject": "metric reproducibility audit (p99/throughput/CPU/memory)",
        "measurement": "none",
        "certified_performance": False,
        "pass": overall,
        "oracle": oracle,
        "completeness": completeness,
        "report_pass_gates": reports,
        "independent_series_reproduction": series,
        "telemetry_overhead": overhead,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--report",
        type=Path,
        help="write the audit JSON report to this path",
    )
    args = parser.parse_args(argv)
    result = run_audit()
    text = json.dumps(result, indent=2) + "\n"
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text, encoding="utf-8")
    sys.stdout.write(text)
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
