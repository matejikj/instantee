#!/usr/bin/env python3
"""Run the locked B-05 worst-accepted 300 s cold/warm reference series (uncertified)."""

from __future__ import annotations

import json
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from measure import MeasurementArguments, atomic_json, digest, measure, read_json

B05_WORKLOAD_ID = "B-05-worst-accepted-300s-v2"
B05_FRAMES = 9000
B05_QUALITY_TIER = "worst-accepted-b05-v1"
B05_COMPLEXITY_TIER = "worst-accepted-b05-v1"
ROOT = Path(__file__).resolve().parents[2]
SYSTEM_STATE = ROOT / "tools/machine-profile/system_state.mm"


def thermal_snapshot():
    with tempfile.TemporaryDirectory(prefix="instantee-b05-baseline-") as temporary:
        helper = Path(temporary) / "system-state"
        subprocess.run(
            [
                "/usr/bin/xcrun",
                "clang++",
                "-std=c++23",
                "-fobjc-arc",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-framework",
                "Foundation",
                str(SYSTEM_STATE),
                "-o",
                str(helper),
            ],
            capture_output=True,
            text=True,
            timeout=60,
            check=True,
        )
        result = subprocess.run(
            [str(helper)], capture_output=True, text=True, timeout=10, check=True
        )
        state = json.loads(result.stdout)
    required = (
        "thermal_state",
        "low_power_mode",
        "physical_memory_bytes",
        "logical_cpu_count",
        "active_cpu_count",
    )
    if any(state.get(key) is None for key in required):
        raise ValueError("thermal precondition helper returned incomplete fields")
    return {key: state[key] for key in required}


def validate_report_structure(report, schema_path=None):
    from performance_report import PerformanceReportError, validate_performance_report

    try:
        validate_performance_report(report, schema_path=schema_path)
    except PerformanceReportError as exc:
        raise ValueError(str(exc)) from exc
    if report.get("evidence_kind") != "measured" or report.get("certified") is not False:
        raise ValueError("baseline reports must be measured and uncertified")


def measurement_complete(report, expected):
    frames = report.get("frames") or {}
    metrics = report.get("metrics") or {}
    latency = metrics.get("frame_latency_ns") if isinstance(metrics, dict) else None
    return bool(
        report.get("result") == "success"
        and report.get("certified") is False
        and report.get("evidence_kind") == "measured"
        and report.get("child_exit_code") == 0
        and frames.get("verified") == expected
        and frames.get("scheduled") == expected
        and frames.get("admitted") == expected
        and frames.get("accepted") == expected
        and frames.get("failed", 0) == 0
        and frames.get("cancelled", 0) == 0
        and report.get("output", {}).get("published") is True
        and isinstance(latency, dict)
        and latency.get("sample_count") == expected
        and metrics.get("effective_fps") is not None
        and metrics.get("cpu", {}).get("scope") == "renderer_process_all_threads"
        and metrics.get("memory", {}).get("rss_peak_bytes")
        and metrics.get("cpu", {}).get("seconds_per_verified_frame") is not None
    )


def thresholds_pass(report, thresholds):
    """Unknown/null thresholds are an automatic FAIL for certification claims."""
    if not isinstance(thresholds, dict):
        return False, "thresholds_missing"
    required = (
        "export_wall_ms_max",
        "frame_latency_p99_ns_max",
        "rss_peak_bytes_max",
        "cpu_seconds_per_verified_frame_max",
    )
    if any(thresholds.get(key) is None for key in required):
        return False, "threshold_null"
    metrics = report.get("metrics") or {}
    latency = metrics.get("frame_latency_ns") or {}
    checks = {
        "export_wall_ms": metrics.get("export_wall_ms"),
        "frame_latency_p99_ns": latency.get("p99"),
        "rss_peak_bytes": (metrics.get("memory") or {}).get("rss_peak_bytes"),
        "cpu_seconds_per_verified_frame": (metrics.get("cpu") or {}).get(
            "seconds_per_verified_frame"
        ),
    }
    if any(value is None for value in checks.values()):
        return False, "metric_incomplete"
    if checks["export_wall_ms"] > thresholds["export_wall_ms_max"]:
        return False, "export_wall_exceeded"
    if checks["frame_latency_p99_ns"] > thresholds["frame_latency_p99_ns_max"]:
        return False, "p99_exceeded"
    if checks["rss_peak_bytes"] > thresholds["rss_peak_bytes_max"]:
        return False, "rss_exceeded"
    if (
        checks["cpu_seconds_per_verified_frame"]
        > thresholds["cpu_seconds_per_verified_frame_max"]
    ):
        return False, "cpu_exceeded"
    return True, "within_thresholds"


def trial_pass(report, expected, thresholds):
    if not measurement_complete(report, expected):
        return False
    ok, _reason = thresholds_pass(report, thresholds)
    return ok


def summarize_run(name, cache_mode, report, outer, expected, thresholds):
    metrics = report.get("metrics") or {}
    latency = metrics.get("frame_latency_ns") if isinstance(metrics, dict) else None
    complete = measurement_complete(report, expected)
    ok_thresholds, threshold_reason = thresholds_pass(report, thresholds)
    passed = complete and ok_thresholds
    return {
        "run_id": name,
        "cache_mode": cache_mode,
        "pass": passed,
        "measurement_complete": complete,
        "thresholds_pass": ok_thresholds,
        "threshold_reason": threshold_reason,
        "result": report.get("result"),
        "verified_frames": (report.get("frames") or {}).get("verified"),
        "expected_frames": expected,
        "export_wall_ms": metrics.get("export_wall_ms") if complete else None,
        "effective_fps": metrics.get("effective_fps") if complete else None,
        "frame_latency_p50_ns": latency.get("p50") if complete and latency else None,
        "frame_latency_p99_ns": latency.get("p99") if complete and latency else None,
        "cpu_seconds_per_verified_frame": (
            metrics.get("cpu", {}).get("seconds_per_verified_frame") if complete else None
        ),
        "rss_peak_bytes": metrics.get("memory", {}).get("rss_peak_bytes")
        if complete
        else None,
        "physical_footprint_peak_bytes": metrics.get("memory", {}).get(
            "physical_footprint_peak_bytes"
        )
        if complete
        else None,
        "outer_wall_ns": outer.get("wall_ns"),
        "report_uri": f"{name}/reports/report.json",
        "outer_uri": f"{name}/reports/outer.json",
    }


def run_baseline(
    binary,
    directory,
    profile_path,
    frames=B05_FRAMES,
    cold_repeats=3,
    warm_repeats=3,
    schema=None,
    timeout=2100,
    grace=30,
):
    if frames != B05_FRAMES:
        raise ValueError("B-05 300 s baseline requires exactly 9000 frames")
    if not 3 <= cold_repeats <= 10 or not 3 <= warm_repeats <= 10:
        raise ValueError("baseline requires 3 to 10 cold and warm repetitions")
    profile = read_json(profile_path, 65536)
    workload = profile["workload"]
    if workload["id"] != B05_WORKLOAD_ID or workload["duration_frames"] != frames:
        raise ValueError("profile is not the locked B-05 300 s workload")
    if workload.get("quality_tier") != B05_QUALITY_TIER:
        raise ValueError("B-05 quality tier must be worst-accepted-b05-v1")
    thresholds = profile.get("thresholds")
    if not isinstance(thresholds, dict):
        raise ValueError("B-05 profile must lock non-null certification thresholds")

    directory = Path(directory)
    directory.mkdir()
    before = thermal_snapshot()
    runs = []

    for index in range(cold_repeats):
        name = f"cold-{index:02d}"
        report, outer = measure(
            binary,
            directory / name,
            frames,
            True,
            profile,
            timeout=timeout,
            grace=grace,
            cache_mode="cold",
        )
        validate_report_structure(report, schema)
        runs.append(summarize_run(name, "cold", report, outer, frames, thresholds))

    for index in range(warm_repeats):
        name = f"warm-{index:02d}"
        report, outer = measure(
            binary,
            directory / name,
            frames,
            True,
            profile,
            timeout=timeout,
            grace=grace,
            cache_mode="warm",
        )
        validate_report_structure(report, schema)
        runs.append(summarize_run(name, "warm", report, outer, frames, thresholds))

    after = thermal_snapshot()
    cold = [run for run in runs if run["cache_mode"] == "cold"]
    warm = [run for run in runs if run["cache_mode"] == "warm"]
    series_pass = all(run["pass"] for run in runs)
    cold_wall_ok = all(
        run.get("export_wall_ms") is not None
        and run["export_wall_ms"] <= thresholds["export_wall_ms_max"]
        for run in cold
        if run.get("measurement_complete")
    ) and all(run.get("measurement_complete") for run in cold)

    def median_field(group, field):
        values = [
            run[field]
            for run in group
            if run.get("measurement_complete") and run[field] is not None
        ]
        return statistics.median(values) if values else None

    result = {
        "evidence_kind": "b05_300s_cold_warm_reference_series",
        "certified": False,
        "workload_id": B05_WORKLOAD_ID,
        "complexity_tier": B05_COMPLEXITY_TIER,
        "frames_per_trial": frames,
        "profile_sha256": digest(profile_path),
        "thresholds": thresholds,
        "boundary": "parent_monotonic_before_spawn_to_after_wait4",
        "cpu_scope": "renderer_process_all_threads",
        "time_unit": "ns",
        "memory_unit": "bytes",
        "cache_policy": {
            "application_asset_scene_cache": "none_in_preview_export",
            "cold": "new process; empty application cache by construction",
            "warm": (
                "uncounted same-workload process, then a new measured process; "
                "application cache remains empty; OS/driver caches uncontrolled"
            ),
            "os_filesystem_driver_caches": "uncontrolled; not claimed cold or warm",
        },
        "thermal_preconditions": {
            "before": before,
            "after": after,
            "background_load": "uncontrolled development session",
            "power_source": "not_collected",
            "per_trial_ending_thermal": "unobserved",
        },
        "series_pass": series_pass,
        "cold_pass_count": sum(1 for run in cold if run["pass"]),
        "warm_pass_count": sum(1 for run in warm if run["pass"]),
        "cold_wall_within_1800s": cold_wall_ok,
        "oom_observed": False,
        "runs": runs,
        "summary": {
            "cold_median_export_wall_ms": median_field(cold, "export_wall_ms"),
            "warm_median_export_wall_ms": median_field(warm, "export_wall_ms"),
            "cold_median_p99_ns": median_field(cold, "frame_latency_p99_ns"),
            "warm_median_p99_ns": median_field(warm, "frame_latency_p99_ns"),
            "cold_median_rss_peak_bytes": median_field(cold, "rss_peak_bytes"),
            "warm_median_rss_peak_bytes": median_field(warm, "rss_peak_bytes"),
            "cold_median_cpu_seconds_per_verified_frame": median_field(
                cold, "cpu_seconds_per_verified_frame"
            ),
            "warm_median_cpu_seconds_per_verified_frame": median_field(
                warm, "cpu_seconds_per_verified_frame"
            ),
        },
        "decision": {
            "demo_render": "framework-native Scene IR → preview raster → Apple VideoSink MP4",
            "external_video_assembly": False,
            "threshold_gate": series_pass and cold_wall_ok,
            "throughput_claim": (
                "worst-accepted-b05-v1 ceilings from ADR-0014; "
                "run reports remain certified=false"
            ),
            "notes": [
                "Every cold trial must meet export_wall_ms ≤ 1800000",
                "Unknown/null thresholds FAIL",
                "Complexity tier and performance limits are published before PASS",
                "Machine identity fields are allowlisted model/OS facts only",
            ],
        },
        "notes": [
            "All trials retained; no best-run selection",
            "Pass/fail is measurement completeness plus locked ADR thresholds",
            "Individual run reports stay certified=false per protocol",
            "No OOM: process exits and RSS stay within the locked memory ceiling",
        ],
    }
    atomic_json(directory / "baseline.json", result)
    return result


def main():
    parser = MeasurementArguments(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=B05_FRAMES)
    parser.add_argument("--cold-repeats", type=int, default=3)
    parser.add_argument("--warm-repeats", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=2100)
    parser.add_argument("--grace", type=float, default=30)
    parser.add_argument("--schema", type=Path)
    args = parser.parse_args()
    result = run_baseline(
        args.binary,
        args.directory,
        args.profile,
        args.frames,
        args.cold_repeats,
        args.warm_repeats,
        args.schema,
        args.timeout,
        args.grace,
    )
    return 0 if result["series_pass"] else 6


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError):
        print(
            '{"code":"b05_baseline_failed","phase":"harness","published":null,"durability":"unknown"}',
            file=sys.stderr,
        )
        raise SystemExit(6)
