#!/usr/bin/env python3
"""Run the P3 geometry/text/mask 60 s cold/warm reference series (uncertified)."""

from pathlib import Path
import statistics
import sys

from baseline import (summarize_run, thermal_snapshot, trial_pass,
                      validate_report_structure)
from measure import MeasurementArguments, atomic_json, digest, measure, read_json

P3_WORKLOAD_ID = "P3-geometry-text-mask-60s-v1"
P3_FRAMES = 1800


def run_series(binary, directory, profile_path, frames=P3_FRAMES, cold_repeats=3,
               warm_repeats=3, schema=None, timeout=1200, grace=30):
    if frames != P3_FRAMES:
        raise ValueError("P3 60 s gate series requires exactly 1800 frames")
    if not 3 <= cold_repeats <= 10 or not 3 <= warm_repeats <= 10:
        raise ValueError("series requires 3 to 10 cold and warm repetitions")
    profile = read_json(profile_path, 65536)
    workload = profile["workload"]
    if workload["id"] != P3_WORKLOAD_ID or workload["duration_frames"] != frames:
        raise ValueError("profile is not the locked P3 geometry/text/mask workload")
    if workload.get("quality_tier") != "p3-foundation-draft":
        raise ValueError("P3 quality tier must be p3-foundation-draft")

    directory = Path(directory)
    directory.mkdir()
    before = thermal_snapshot()
    runs = []

    for index in range(cold_repeats):
        name = f"cold-{index:02d}"
        report, outer = measure(binary, directory / name, frames, True, profile,
                                timeout=timeout, grace=grace, cache_mode="cold")
        validate_report_structure(report, schema)
        runs.append(summarize_run(name, "cold", report, outer, frames))

    for index in range(warm_repeats):
        name = f"warm-{index:02d}"
        report, outer = measure(binary, directory / name, frames, True, profile,
                                timeout=timeout, grace=grace, cache_mode="warm")
        validate_report_structure(report, schema)
        runs.append(summarize_run(name, "warm", report, outer, frames))

    after = thermal_snapshot()
    cold = [run for run in runs if run["cache_mode"] == "cold"]
    warm = [run for run in runs if run["cache_mode"] == "warm"]
    series_pass = all(run["pass"] for run in runs)

    def median_field(group, field):
        values = [run[field] for run in group if run["pass"] and run[field] is not None]
        return statistics.median(values) if values else None

    def max_field(group, field):
        values = [run[field] for run in group if run["pass"] and run[field] is not None]
        return max(values) if values else None

    result = {
        "evidence_kind": "p3_geometry_text_mask_60s_cold_warm",
        "certified": False,
        "workload_id": P3_WORKLOAD_ID,
        "frames_per_trial": frames,
        "profile_sha256": digest(profile_path),
        "boundary": "parent_monotonic_before_spawn_to_after_wait4",
        "cpu_scope": "renderer_process_all_threads",
        "time_unit": "ns",
        "memory_unit": "bytes",
        "runtime_capacities": {
            "K_in_flight_credits": 2,
            "queue_capacity": 1,
            "E_encoder_pool": 8,
        },
        "cache_policy": {
            "application_asset_scene_cache": "none_in_p3_gate_metrics",
            "cold": "new process; empty application cache by construction",
            "warm": ("uncounted same-workload process, then a new measured process; "
                     "application cache remains empty; OS/driver caches uncontrolled"),
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
        "runs": runs,
        "summary": {
            "cold_median_export_wall_ms": median_field(cold, "export_wall_ms"),
            "warm_median_export_wall_ms": median_field(warm, "export_wall_ms"),
            "cold_median_p99_ns": median_field(cold, "frame_latency_p99_ns"),
            "warm_median_p99_ns": median_field(warm, "frame_latency_p99_ns"),
            "cold_max_p99_ns": max_field(cold, "frame_latency_p99_ns"),
            "warm_max_p99_ns": max_field(warm, "frame_latency_p99_ns"),
            "cold_median_rss_peak_bytes": median_field(cold, "rss_peak_bytes"),
            "warm_median_rss_peak_bytes": median_field(warm, "rss_peak_bytes"),
            "cold_max_rss_peak_bytes": max_field(cold, "rss_peak_bytes"),
            "warm_max_rss_peak_bytes": max_field(warm, "rss_peak_bytes"),
            "cold_median_cpu_seconds_per_verified_frame":
                median_field(cold, "cpu_seconds_per_verified_frame"),
            "warm_median_cpu_seconds_per_verified_frame":
                median_field(warm, "cpu_seconds_per_verified_frame"),
        },
        "notes": [
            "All trials retained; no best-run selection",
            "Pass/fail is measurement completeness for the locked P3 workload",
            "Draft thresholds for later B-0x are derived separately in ADR-0014",
            "This series itself does not certify product throughput SLOs",
        ],
    }
    atomic_json(directory / "p3_gate_series.json", result)
    return result


def main():
    parser = MeasurementArguments(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=P3_FRAMES)
    parser.add_argument("--cold-repeats", type=int, default=3)
    parser.add_argument("--warm-repeats", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=1200)
    parser.add_argument("--grace", type=float, default=30)
    parser.add_argument("--schema", type=Path)
    args = parser.parse_args()
    result = run_series(args.binary, args.directory, args.profile, args.frames,
                        args.cold_repeats, args.warm_repeats, args.schema,
                        args.timeout, args.grace)
    print(f"series_pass={result['series_pass']} cold={result['cold_pass_count']} "
          f"warm={result['warm_pass_count']}")
    return 0 if result["series_pass"] else 2


if __name__ == "__main__":
    sys.exit(main())
