#!/usr/bin/env python3
"""Run the B-04/B-05 300 s thermal and memory endurance suite (uncertified).

measurement=none: this suite checks fatal slowdown and working-set growth under
declared anti-leak / capacity budgets. It does not certify throughput, p99, or
CPU SLOs. Individual run reports remain certified=false.
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from endurance import (  # noqa: E402
    CAPACITY_E,
    CAPACITY_K,
    CAPACITY_QUEUE,
    RSS_HARD_BUDGET_BYTES,
    RSS_SOFT_BUDGET_BYTES,
    analyze_resource_series,
    capacities_ok,
    measure_with_thermal,
    oldest_slow_frame,
)
from baseline import summarize_run, thermal_snapshot, trial_pass, validate_report_structure
from measure import atomic_json, digest, read_json

ROOT = Path(__file__).resolve().parents[2]

FAMILY_SPECS: dict[str, dict[str, Any]] = {
    "B-04": {
        "workload_id": "B-04-mixed-300s-v2",
        "quality_tier": "mixed-b04-v1",
        "frames": 9000,
        "evidence_kind": "b04_300s_cold_warm_endurance",
    },
    "B-05": {
        "workload_id": "B-05-worst-accepted-300s-v2",
        "quality_tier": "worst-accepted-b05-v1",
        "frames": 9000,
        "evidence_kind": "b05_300s_cold_warm_endurance",
    },
}

# Late-half median latency above this multiple of the early-half median is fatal.
SLOWDOWN_RATIO_MAX = 3.0
# Post-warmup projected RSS growth above half the soft budget is fatal.
RSS_GROWTH_LIMIT_BYTES = RSS_SOFT_BUDGET_BYTES // 2


def frame_latency_ns(row: list[int]) -> int:
    return row[8] - row[1]


def analyze_fps_and_slowdown(
    frames: Any, *, warmup_fraction: float = 0.2, bucket_frames: int = 300
) -> dict[str, Any] | None:
    """Build fps buckets and detect fatal post-warmup latency slowdown."""
    if not isinstance(frames, list) or len(frames) < 2:
        return None
    samples: list[tuple[int, int, int]] = []
    for row in frames:
        if (
            not isinstance(row, list)
            or len(row) != 9
            or not all(isinstance(v, int) and v >= 0 for v in row)
        ):
            return None
        latency = frame_latency_ns(row)
        if latency < 0:
            return None
        samples.append((row[0], row[8], latency))
    if len(samples) < 2:
        return None

    start = max(1, int(len(samples) * warmup_fraction))
    active = samples[start:]
    if len(active) < 4:
        active = samples
    mid = len(active) // 2
    early = [latency for _, _, latency in active[:mid]]
    late = [latency for _, _, latency in active[mid:]]
    if not early or not late:
        return None
    early_median = statistics.median(early)
    late_median = statistics.median(late)
    ratio = None if early_median <= 0 else late_median / early_median
    fatal_slowdown = ratio is not None and ratio > SLOWDOWN_RATIO_MAX

    buckets: list[dict[str, Any]] = []
    for index in range(0, len(samples), bucket_frames):
        window = samples[index : index + bucket_frames]
        if len(window) < 2:
            continue
        first_ts = window[0][1]
        last_ts = window[-1][1]
        span_ns = max(1, last_ts - first_ts)
        buckets.append(
            {
                "frame_start": window[0][0],
                "frame_end": window[-1][0],
                "frame_count": len(window),
                "span_ns": span_ns,
                "fps": len(window) / (span_ns / 1e9),
                "median_latency_ns": statistics.median(
                    latency for _, _, latency in window
                ),
            }
        )

    return {
        "warmup_fraction": warmup_fraction,
        "bucket_frames": bucket_frames,
        "early_median_latency_ns": early_median,
        "late_median_latency_ns": late_median,
        "late_over_early_latency_ratio": ratio,
        "fatal_slowdown": fatal_slowdown,
        "fps_buckets": buckets,
    }


def working_set_within_budget(
    series: dict[str, Any] | None, rss_budget_bytes: int
) -> tuple[bool, str]:
    if series is None:
        return False, "resource_series_missing"
    if series["rss_peak_max_bytes"] > RSS_HARD_BUDGET_BYTES:
        return False, "rss_hard_budget_exceeded"
    if series["rss_peak_max_bytes"] > rss_budget_bytes:
        return False, "declared_rss_budget_exceeded"
    growth = series["post_warmup_rss_projected_growth_bytes"]
    if growth is not None and growth > RSS_GROWTH_LIMIT_BYTES:
        return False, "post_warmup_rss_growth_exceeded"
    return True, "within_budget"


def style_endurance_pass(
    report: dict[str, Any],
    expected: int,
    child: dict[str, Any],
    resources: Any,
    frames: Any,
    rss_budget_bytes: int,
) -> tuple[bool, dict[str, Any]]:
    details: dict[str, Any] = {
        "measurement_complete": trial_pass(report, expected),
        "capacities_ok": capacities_ok(child),
        "oldest_slow_frame": oldest_slow_frame(frames),
        "resource_series": analyze_resource_series(resources),
        "fps_series": analyze_fps_and_slowdown(frames),
    }
    budget_ok, budget_reason = working_set_within_budget(
        details["resource_series"], rss_budget_bytes
    )
    details["working_set_ok"] = budget_ok
    details["working_set_reason"] = budget_reason
    details["fatal_slowdown"] = bool(
        details["fps_series"] and details["fps_series"]["fatal_slowdown"]
    )
    details["error_count"] = {
        "failed_frames": (report.get("frames") or {}).get("failed", 0),
        "cancelled_frames": (report.get("frames") or {}).get("cancelled", 0),
        "frame_lost_samples": child.get("frame_lost_samples"),
        "resource_lost_samples": child.get("resource_lost_samples"),
    }
    passed = bool(
        details["measurement_complete"]
        and details["capacities_ok"]
        and details["oldest_slow_frame"] is not None
        and details["resource_series"] is not None
        and details["fps_series"] is not None
        and budget_ok
        and not details["fatal_slowdown"]
        and details["error_count"]["failed_frames"] == 0
        and details["error_count"]["cancelled_frames"] == 0
        and details["error_count"]["frame_lost_samples"] == 0
        and details["error_count"]["resource_lost_samples"] == 0
    )
    return passed, details


def render_ascii_plot(
    buckets: list[dict[str, Any]], *, width: int = 48, height: int = 8
) -> list[str]:
    """Compact fps sparkline for the endurance plot artifact."""
    if not buckets:
        return ["(no fps buckets)"]
    values = [float(bucket["fps"]) for bucket in buckets]
    lo = min(values)
    hi = max(values)
    span = max(1e-9, hi - lo)
    lines = [f"fps [{lo:.2f} .. {hi:.2f}] over {len(values)} buckets"]
    grid = [[" " for _ in range(width)] for _ in range(height)]
    for index, value in enumerate(values):
        x = int(index * (width - 1) / max(1, len(values) - 1))
        y = int((value - lo) / span * (height - 1))
        grid[height - 1 - y][x] = "*"
    lines.extend("|" + "".join(row) + "|" for row in grid)
    return lines


def summarize_style_run(
    name: str,
    cache_mode: str,
    report: dict[str, Any],
    outer: dict[str, Any],
    expected: int,
    bundle: Path,
    rss_budget_bytes: int,
    thermal_series: list[dict[str, Any]],
) -> dict[str, Any]:
    summary = summarize_run(name, cache_mode, report, outer, expected)
    child = read_json(bundle / "output/child.json")
    resources = read_json(bundle / "output/resources.json")
    frames = read_json(bundle / "output/frames.json")
    passed, details = style_endurance_pass(
        report, expected, child, resources, frames, rss_budget_bytes
    )
    thermal_states = [
        sample.get("thermal_state")
        for sample in thermal_series
        if isinstance(sample, dict)
    ]
    summary.update(
        {
            "pass": passed,
            "in_flight_peak": child.get("in_flight_peak"),
            "queue_peak": child.get("queue_peak"),
            "encoder_pool_peak": child.get("encoder_pool_peak"),
            "frame_lost_samples": child.get("frame_lost_samples"),
            "resource_lost_samples": child.get("resource_lost_samples"),
            "resource_series": details["resource_series"],
            "fps_series": {
                key: details["fps_series"][key]
                for key in (
                    "warmup_fraction",
                    "bucket_frames",
                    "early_median_latency_ns",
                    "late_median_latency_ns",
                    "late_over_early_latency_ratio",
                    "fatal_slowdown",
                )
            }
            if details["fps_series"]
            else None,
            "fps_buckets": (details["fps_series"] or {}).get("fps_buckets"),
            "oldest_slow_frame": details["oldest_slow_frame"],
            "working_set_reason": details["working_set_reason"],
            "error_count": details["error_count"],
            "thermal_states_observed": sorted(
                {state for state in thermal_states if state is not None}
            ),
            "thermal_sample_count": len(thermal_series),
            "thermal_series_uri": f"{name}/reports/thermal_series.json",
            "shutdown": {
                "child_exit_code": report.get("child_exit_code"),
                "published": report.get("output", {}).get("published"),
                "timed_out": outer.get("timed_out"),
                "cancelled": outer.get("cancelled"),
            },
        }
    )
    return summary


def declared_rss_budget(profile: dict[str, Any]) -> int:
    thresholds = profile.get("thresholds")
    if isinstance(thresholds, dict):
        value = thresholds.get("rss_peak_bytes_max")
        if isinstance(value, int) and value > 0:
            return value
    return RSS_SOFT_BUDGET_BYTES


def run_family_endurance(
    family: str,
    binary: Path,
    directory: Path,
    profile_path: Path,
    *,
    cold_repeats: int = 1,
    warm_repeats: int = 1,
    schema: Path | None = None,
    timeout: float = 2100,
    grace: float = 60,
    thermal_interval: float = 5.0,
) -> dict[str, Any]:
    spec = FAMILY_SPECS[family]
    frames = spec["frames"]
    if not 1 <= cold_repeats <= 10 or not 1 <= warm_repeats <= 10:
        raise ValueError("style endurance requires 1 to 10 cold and warm repetitions")
    profile = read_json(profile_path, 65536)
    workload = profile["workload"]
    if workload["id"] != spec["workload_id"] or workload["duration_frames"] != frames:
        raise ValueError(f"profile is not the locked {family} 300 s workload")
    if workload.get("quality_tier") != spec["quality_tier"]:
        raise ValueError(f"{family} quality tier mismatch")
    rss_budget = declared_rss_budget(profile)

    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    before = thermal_snapshot()
    runs: list[dict[str, Any]] = []

    for index in range(cold_repeats):
        name = f"cold-{index:02d}"
        bundle = directory / name
        report, outer, thermal = measure_with_thermal(
            binary, bundle, frames, profile, timeout, grace, "cold", thermal_interval
        )
        validate_report_structure(report, schema)
        runs.append(
            summarize_style_run(
                name, "cold", report, outer, frames, bundle, rss_budget, thermal
            )
        )

    for index in range(warm_repeats):
        name = f"warm-{index:02d}"
        bundle = directory / name
        report, outer, thermal = measure_with_thermal(
            binary, bundle, frames, profile, timeout, grace, "warm", thermal_interval
        )
        validate_report_structure(report, schema)
        runs.append(
            summarize_style_run(
                name, "warm", report, outer, frames, bundle, rss_budget, thermal
            )
        )

    after = thermal_snapshot()
    series_pass = all(run["pass"] for run in runs)
    plot_lines: list[str] = []
    for run in runs:
        plot_lines.append(f"## {family} {run['run_id']} ({run['cache_mode']})")
        plot_lines.append(f"pass={run['pass']} wall_ms={run.get('export_wall_ms')}")
        plot_lines.extend(render_ascii_plot(run.get("fps_buckets") or []))
        plot_lines.append("")

    result = {
        "evidence_kind": spec["evidence_kind"],
        "certified": False,
        "performance_claim": False,
        "family": family,
        "workload_id": spec["workload_id"],
        "frames_per_trial": frames,
        "profile_sha256": digest(profile_path),
        "boundary": "parent_monotonic_before_spawn_to_after_wait4",
        "cpu_scope": "renderer_process_all_threads",
        "time_unit": "ns",
        "memory_unit": "bytes",
        "declared_budgets": {
            "frame_credits_k": CAPACITY_K,
            "queue_capacity": CAPACITY_QUEUE,
            "encoder_pool_e": CAPACITY_E,
            "rss_soft_budget_bytes": RSS_SOFT_BUDGET_BYTES,
            "rss_hard_budget_bytes": RSS_HARD_BUDGET_BYTES,
            "rss_declared_peak_budget_bytes": rss_budget,
            "rss_growth_limit_bytes": RSS_GROWTH_LIMIT_BYTES,
            "slowdown_ratio_max": SLOWDOWN_RATIO_MAX,
        },
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
            "per_trial_series": "allowlisted Foundation samples during each measured process",
        },
        "series_pass": series_pass,
        "cold_pass_count": sum(
            1 for run in runs if run["cache_mode"] == "cold" and run["pass"]
        ),
        "warm_pass_count": sum(
            1 for run in runs if run["cache_mode"] == "warm" and run["pass"]
        ),
        "runs": runs,
        "ascii_plot_lines": plot_lines,
        "notes": [
            "measurement=none: anti-leak / thermal / capacity endurance only",
            "No throughput, p99, or CPU certification claim",
            "All trials retained; no best-run selection",
            "Declared RSS peak budget comes from the locked profile when present",
        ],
    }
    atomic_json(directory / "endurance.json", result)
    (directory / "endurance-plot.md").write_text(
        "\n".join(plot_lines) + "\n", encoding="utf-8"
    )
    return result


def assemble_suite(families: dict[str, dict[str, Any]]) -> dict[str, Any]:
    missing = [name for name in FAMILY_SPECS if name not in families]
    if missing:
        raise ValueError(f"missing endurance families: {','.join(missing)}")
    suite_pass = all(families[name].get("series_pass") is True for name in FAMILY_SPECS)
    return {
        "evidence_kind": "b04_b05_300s_thermal_endurance_suite",
        "certified": False,
        "performance_claim": False,
        "pass": suite_pass,
        "families": {
            name: {
                "pass": families[name].get("series_pass") is True,
                "workload_id": families[name].get("workload_id"),
                "cold_pass_count": families[name].get("cold_pass_count"),
                "warm_pass_count": families[name].get("warm_pass_count"),
                "declared_budgets": families[name].get("declared_budgets"),
                "runs": [
                    {
                        "run_id": run.get("run_id"),
                        "cache_mode": run.get("cache_mode"),
                        "pass": run.get("pass"),
                        "export_wall_ms": run.get("export_wall_ms"),
                        "effective_fps": run.get("effective_fps"),
                        "rss_peak_bytes": run.get("rss_peak_bytes"),
                        "in_flight_peak": run.get("in_flight_peak"),
                        "queue_peak": run.get("queue_peak"),
                        "encoder_pool_peak": run.get("encoder_pool_peak"),
                        "working_set_reason": run.get("working_set_reason"),
                        "fatal_slowdown": (run.get("fps_series") or {}).get(
                            "fatal_slowdown"
                        ),
                        "late_over_early_latency_ratio": (run.get("fps_series") or {}).get(
                            "late_over_early_latency_ratio"
                        ),
                        "thermal_states_observed": run.get("thermal_states_observed"),
                        "error_count": run.get("error_count"),
                        "resource_series": {
                            key: (run.get("resource_series") or {}).get(key)
                            for key in (
                                "sample_count",
                                "rss_peak_max_bytes",
                                "rss_current_max_bytes",
                                "post_warmup_rss_projected_growth_bytes",
                            )
                        }
                        if run.get("resource_series")
                        else None,
                    }
                    for run in families[name].get("runs") or []
                ],
            }
            for name in FAMILY_SPECS
        },
        "decision": {
            "endurance_gate": suite_pass,
            "notes": [
                "Pass requires complete samples, K/E/queue bounds, no silent drops",
                "No fatal post-warmup latency slowdown",
                "Working set stays within declared RSS peak and growth budgets",
                "Not a throughput/p99/CPU certificate",
            ],
        },
        "notes": [
            "Suite covers locked B-04 mixed and B-05 worst-accepted 300 s reels",
            "Plots are fps bucket sparklines derived from per-frame timestamps",
        ],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--b04-binary", type=Path, required=True)
    parser.add_argument("--b04-profile", type=Path, required=True)
    parser.add_argument("--b05-binary", type=Path, required=True)
    parser.add_argument("--b05-profile", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--cold-repeats", type=int, default=1)
    parser.add_argument("--warm-repeats", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=2100)
    parser.add_argument("--grace", type=float, default=60)
    parser.add_argument("--thermal-interval", type=float, default=5)
    parser.add_argument("--schema", type=Path)
    args = parser.parse_args(argv)

    root = Path(args.directory)
    root.mkdir(parents=True, exist_ok=True)
    families = {
        "B-04": run_family_endurance(
            "B-04",
            args.b04_binary,
            root / "B-04",
            args.b04_profile,
            cold_repeats=args.cold_repeats,
            warm_repeats=args.warm_repeats,
            schema=args.schema,
            timeout=args.timeout,
            grace=args.grace,
            thermal_interval=args.thermal_interval,
        ),
        "B-05": run_family_endurance(
            "B-05",
            args.b05_binary,
            root / "B-05",
            args.b05_profile,
            cold_repeats=args.cold_repeats,
            warm_repeats=args.warm_repeats,
            schema=args.schema,
            timeout=args.timeout,
            grace=args.grace,
            thermal_interval=args.thermal_interval,
        ),
    }
    suite = assemble_suite(families)
    plot_parts = ["# B-04/B-05 300 s endurance plots", ""]
    for family in ("B-04", "B-05"):
        plot_parts.extend(families[family].get("ascii_plot_lines") or [])
    atomic_json(root / "suite.json", suite)
    (root / "endurance-plot.md").write_text("\n".join(plot_parts) + "\n", encoding="utf-8")
    print(root / "suite.json")
    return 0 if suite["pass"] else 6


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError):
        print(
            '{"code":"style_endurance_failed","phase":"harness","published":null,"durability":"unknown"}',
            file=sys.stderr,
        )
        raise SystemExit(6)
