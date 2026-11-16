#!/usr/bin/env python3
"""Run the locked B-00 300 s cold/warm endurance series without self-certifying."""

from pathlib import Path
import statistics
import subprocess
import sys
import threading
import time

from baseline import (summarize_run, thermal_snapshot, trial_pass,
                      validate_report_structure)
from measure import MeasurementArguments, atomic_json, digest, measure, read_json

B00_300_WORKLOAD_ID = "B-00-test-pattern-300s-v1"
B00_300_FRAMES = 9000
# Declared pipeline capacities of spike-export-metrics; peaks must stay inside.
CAPACITY_K = 2
CAPACITY_QUEUE = 1
CAPACITY_E = 8
# Soft RSS budget used only to reject unbounded growth, not as a certified SLO.
RSS_SOFT_BUDGET_BYTES = 4 * 1024**3
RSS_HARD_BUDGET_BYTES = 6 * 1024**3


def linear_slope(xs, ys):
    """Ordinary least-squares slope for equal-length numeric series."""
    if len(xs) != len(ys) or len(xs) < 2:
        return None
    mean_x = sum(xs) / len(xs)
    mean_y = sum(ys) / len(ys)
    denom = sum((x - mean_x) ** 2 for x in xs)
    if denom == 0:
        return 0.0
    return sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys)) / denom


def analyze_resource_series(resources, warmup_fraction=0.2):
    """Summarize RSS/footprint/CPU time series and post-warmup RSS slope."""
    if not isinstance(resources, list) or len(resources) < 2:
        return None
    observed = []
    for row in resources:
        if (not isinstance(row, list) or len(row) != 6 or
                not all(type(v) is int and v >= 0 for v in row[:5])):
            return None
        if row[5] is not None and not (type(row[5]) is int and row[5] >= 0):
            return None
        observed.append(row)
    if len(observed) < 2:
        return None
    start = max(1, int(len(observed) * warmup_fraction))
    window = observed[start:]
    if len(window) < 2:
        window = observed
    xs = [row[0] - window[0][0] for row in window]
    rss = [row[3] for row in window]
    slope = linear_slope(xs, rss)
    duration_ns = max(1, observed[-1][0] - observed[0][0])
    projected_growth = None if slope is None else slope * (window[-1][0] - window[0][0])
    footprints = [row[5] for row in observed if row[5] is not None]
    return {
        "sample_count": len(observed),
        "duration_ns": duration_ns,
        "rss_current_min_bytes": min(row[3] for row in observed),
        "rss_current_max_bytes": max(row[3] for row in observed),
        "rss_peak_max_bytes": max(row[4] for row in observed),
        "physical_footprint_max_bytes": max(footprints) if footprints else None,
        "user_cpu_ns_end": observed[-1][1],
        "system_cpu_ns_end": observed[-1][2],
        "warmup_fraction": warmup_fraction,
        "post_warmup_sample_count": len(window),
        "post_warmup_rss_slope_bytes_per_ns": slope,
        "post_warmup_rss_projected_growth_bytes": projected_growth,
    }


def oldest_slow_frame(frames):
    """Return the earliest frame index among those sharing the maximum latency."""
    if not isinstance(frames, list) or not frames:
        return None
    best = None
    for row in frames:
        if (not isinstance(row, list) or len(row) != 9 or
                not all(type(v) is int and v >= 0 for v in row)):
            return None
        latency = row[8] - row[1]
        if latency < 0:
            return None
        candidate = (latency, row[0])
        if best is None or candidate[0] > best[0] or (candidate[0] == best[0] and candidate[1] < best[1]):
            best = candidate
    return {"frame_index": best[1], "latency_ns": best[0]}


def capacities_ok(child):
    if not isinstance(child, dict):
        return False
    return (child.get("in_flight_peak", 99) <= CAPACITY_K and
            child.get("queue_peak", 99) <= CAPACITY_QUEUE and
            child.get("encoder_pool_peak") == CAPACITY_E and
            child.get("frame_lost_samples", 1) == 0 and
            child.get("resource_lost_samples", 1) == 0)


def endurance_pass(report, expected, child, resources, frames):
    if not trial_pass(report, expected):
        return False
    if not capacities_ok(child):
        return False
    series = analyze_resource_series(resources)
    if series is None or oldest_slow_frame(frames) is None:
        return False
    if series["rss_peak_max_bytes"] > RSS_HARD_BUDGET_BYTES:
        return False
    growth = series["post_warmup_rss_projected_growth_bytes"]
    if growth is not None and growth > RSS_SOFT_BUDGET_BYTES / 2:
        return False
    return True


class ThermalMonitor:
    """Background allowlisted thermal samples for one measured trial."""

    def __init__(self, interval_seconds=5.0):
        self.interval_seconds = interval_seconds
        self.samples = []
        self._stop = threading.Event()
        self._thread = None

    def start(self):
        self.samples.append({"elapsed_ns": 0, **thermal_snapshot()})
        self._thread = threading.Thread(target=self._run, name="thermal-monitor", daemon=True)
        self._thread.start()

    def _run(self):
        started = time.monotonic_ns()
        while not self._stop.wait(self.interval_seconds):
            sample = thermal_snapshot()
            sample["elapsed_ns"] = time.monotonic_ns() - started
            self.samples.append(sample)

    def stop(self):
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=30)
            self._thread = None
        final = thermal_snapshot()
        final["elapsed_ns"] = (self.samples[-1]["elapsed_ns"] + 1) if self.samples else 0
        self.samples.append(final)
        return list(self.samples)


def measure_with_thermal(binary, bundle, expected, profile, timeout, grace, cache_mode,
                         thermal_interval=5.0):
    monitor = ThermalMonitor(thermal_interval)
    monitor.start()
    try:
        report, outer = measure(binary, bundle, expected, True, profile,
                                timeout=timeout, grace=grace, cache_mode=cache_mode)
    finally:
        thermal_series = monitor.stop()
    atomic_json(Path(bundle) / "reports/thermal_series.json", {
        "interval_seconds": thermal_interval,
        "sample_count": len(thermal_series),
        "samples": thermal_series,
        "fields": ["elapsed_ns", "thermal_state", "low_power_mode",
                   "physical_memory_bytes", "logical_cpu_count", "active_cpu_count"],
    })
    return report, outer, thermal_series


def summarize_endurance_run(name, cache_mode, report, outer, expected, bundle):
    summary = summarize_run(name, cache_mode, report, outer, expected)
    child = read_json(Path(bundle) / "output/child.json")
    resources = read_json(Path(bundle) / "output/resources.json")
    frames = read_json(Path(bundle) / "output/frames.json")
    series = analyze_resource_series(resources)
    slow = oldest_slow_frame(frames)
    passed = endurance_pass(report, expected, child, resources, frames)
    summary.update({
        "pass": passed,
        "in_flight_peak": child.get("in_flight_peak"),
        "queue_peak": child.get("queue_peak"),
        "encoder_pool_peak": child.get("encoder_pool_peak"),
        "frame_lost_samples": child.get("frame_lost_samples"),
        "resource_lost_samples": child.get("resource_lost_samples"),
        "resource_series": series,
        "oldest_slow_frame": slow,
        "thermal_series_uri": f"{name}/reports/thermal_series.json",
        "shutdown": {
            "child_exit_code": report.get("child_exit_code"),
            "published": report.get("output", {}).get("published"),
            "timed_out": outer.get("timed_out"),
            "cancelled": outer.get("cancelled"),
        },
    })
    return summary


def run_endurance(binary, directory, profile_path, frames=B00_300_FRAMES, cold_repeats=3,
                  warm_repeats=3, schema=None, timeout=1800, grace=60, thermal_interval=5.0):
    if frames != B00_300_FRAMES:
        raise ValueError("B-00 300 s endurance requires exactly 9000 frames")
    if not 3 <= cold_repeats <= 10 or not 3 <= warm_repeats <= 10:
        raise ValueError("endurance requires 3 to 10 cold and warm repetitions")
    profile = read_json(profile_path, 65536)
    workload = profile["workload"]
    if workload["id"] != B00_300_WORKLOAD_ID or workload["duration_frames"] != frames:
        raise ValueError("profile is not the locked B-00 300 s workload")
    if workload.get("quality_tier") != "feasibility-baseline":
        raise ValueError("B-00 quality tier must be feasibility-baseline")

    directory = Path(directory)
    directory.mkdir()
    before = thermal_snapshot()
    runs = []

    for index in range(cold_repeats):
        name = f"cold-{index:02d}"
        bundle = directory / name
        report, outer, _ = measure_with_thermal(
            binary, bundle, frames, profile, timeout, grace, "cold", thermal_interval)
        validate_report_structure(report, schema)
        runs.append(summarize_endurance_run(name, "cold", report, outer, frames, bundle))

    for index in range(warm_repeats):
        name = f"warm-{index:02d}"
        bundle = directory / name
        report, outer, _ = measure_with_thermal(
            binary, bundle, frames, profile, timeout, grace, "warm", thermal_interval)
        validate_report_structure(report, schema)
        runs.append(summarize_endurance_run(name, "warm", report, outer, frames, bundle))

    after = thermal_snapshot()
    cold = [run for run in runs if run["cache_mode"] == "cold"]
    warm = [run for run in runs if run["cache_mode"] == "warm"]
    series_pass = all(run["pass"] for run in runs)

    def median_field(group, field):
        values = [run[field] for run in group if run["pass"] and run[field] is not None]
        return statistics.median(values) if values else None

    result = {
        "evidence_kind": "b00_300s_cold_warm_endurance",
        "certified": False,
        "workload_id": B00_300_WORKLOAD_ID,
        "frames_per_trial": frames,
        "profile_sha256": digest(profile_path),
        "boundary": "parent_monotonic_before_spawn_to_after_wait4",
        "cpu_scope": "renderer_process_all_threads",
        "time_unit": "ns",
        "memory_unit": "bytes",
        "declared_capacities": {
            "frame_credits_k": CAPACITY_K,
            "queue_capacity": CAPACITY_QUEUE,
            "encoder_pool_e": CAPACITY_E,
            "rss_soft_budget_bytes": RSS_SOFT_BUDGET_BYTES,
            "rss_hard_budget_bytes": RSS_HARD_BUDGET_BYTES,
        },
        "cache_policy": {
            "application_asset_scene_cache": "none_in_spike",
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
            "per_trial_series": "allowlisted Foundation samples during each measured process",
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
            "cold_median_rss_peak_bytes": median_field(cold, "rss_peak_bytes"),
            "warm_median_rss_peak_bytes": median_field(warm, "rss_peak_bytes"),
        },
        "notes": [
            "All trials retained; no best-run selection",
            "Pass/fail requires complete samples, capacity bounds, clean shutdown and bounded RSS growth",
            "p99/CPU/RSS thresholds remain unlocked; no throughput certification",
            "Thermal series are process-level allowlisted samples, not a power-loss study",
        ],
    }
    atomic_json(directory / "endurance.json", result)
    return result


def main():
    parser = MeasurementArguments(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=B00_300_FRAMES)
    parser.add_argument("--cold-repeats", type=int, default=3)
    parser.add_argument("--warm-repeats", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=1800)
    parser.add_argument("--grace", type=float, default=60)
    parser.add_argument("--thermal-interval", type=float, default=5)
    parser.add_argument("--schema", type=Path)
    args = parser.parse_args()
    result = run_endurance(args.binary, args.directory, args.profile, args.frames,
                           args.cold_repeats, args.warm_repeats, args.schema,
                           args.timeout, args.grace, args.thermal_interval)
    return 0 if result["series_pass"] else 6


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError):
        print('{"code":"endurance_failed","phase":"harness","published":null,"durability":"unknown"}',
              file=sys.stderr)
        raise SystemExit(6)
