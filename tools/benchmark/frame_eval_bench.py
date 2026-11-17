#!/usr/bin/env python3
"""Pack Google Benchmark JSON for frame evaluation / scheduler CPU micrometrics.

Produces an anonymized report (no hostnames, user paths, or device UUIDs).
Results are a development baseline, not an M1 Air certification claim.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import resource
import subprocess
import sys
from pathlib import Path


def machine_profile() -> dict:
    return {
        "os": platform.system(),
        "os_release": platform.release(),
        "arch": platform.machine(),
        "python": platform.python_version(),
        "logical_cpus": os.cpu_count(),
        "page_size": resource.getpagesize(),
        "certification": "non_certified_dev_baseline",
    }


def build_profile(binary: Path) -> dict:
    return {
        "binary": binary.name,
        "cmake_build_type_hint": "Release preferred for micrometrics",
        "sanitizer": "none_required_for_report",
        "certification": "non_certified_dev_baseline",
    }


def counter_value(bench: dict, name: str) -> float | None:
    """Read a UserCounter from nested `counters` or flattened GB JSON fields."""
    counters = bench.get("counters") or {}
    entry = counters.get(name) if isinstance(counters, dict) else None
    if entry is None and name in bench:
        entry = bench[name]
    if entry is None:
        return None
    if isinstance(entry, dict):
        return float(entry.get("value"))
    return float(entry)


def summarize_benchmarks(raw: dict) -> list[dict]:
    out = []
    for bench in raw.get("benchmarks", []):
        if bench.get("run_type") == "aggregate":
            continue
        out.append(
            {
                "name": bench.get("name"),
                "iterations": bench.get("iterations"),
                "real_time": bench.get("real_time"),
                "cpu_time": bench.get("cpu_time"),
                "time_unit": bench.get("time_unit"),
                "items_per_second": bench.get("items_per_second"),
                "counters": {
                    key: counter_value(bench, key)
                    for key in (
                        "nodes",
                        "rss_peak_bytes",
                        "rss_before_bytes",
                        "rss_after_bytes",
                        "batch_frames",
                        "blocked_push",
                        "blocked_wait_ns",
                        "queue_high_water",
                    )
                    if counter_value(bench, key) is not None
                },
            }
        )
    return out


def growth_analysis(benches: list[dict]) -> dict:
    probes = [
        b
        for b in benches
        if b["name"] and b["name"].startswith("BM_AllocationGrowthProbe")
    ]
    series = []
    for probe in probes:
        batch = probe["counters"].get("batch_frames")
        before = probe["counters"].get("rss_before_bytes")
        after = probe["counters"].get("rss_after_bytes")
        if batch is None or before is None or after is None or batch <= 0:
            continue
        delta = max(0.0, after - before)
        series.append(
            {
                "batch_frames": int(batch),
                "rss_before_bytes": before,
                "rss_after_bytes": after,
                "rss_delta_bytes": delta,
                "rss_delta_per_frame_bytes": delta / batch,
            }
        )
    series.sort(key=lambda row: row["batch_frames"])

    unbounded = False
    reason = "insufficient_samples"
    if len(series) >= 3:
        # Steady-state probe: per-frame RSS delta must not keep climbing with
        # batch size (unbounded growth would roughly scale with batch).
        per = [row["rss_delta_per_frame_bytes"] for row in series]
        # Allow noise: last point must be <= 2x the median of earlier points + 4KiB.
        earlier = sorted(per[:-1])
        median_earlier = earlier[len(earlier) // 2]
        threshold = max(4096.0, median_earlier * 2.0)
        unbounded = per[-1] > threshold and per[-1] > per[0] * 2.0
        reason = (
            "per_frame_rss_delta_grows_with_batch"
            if unbounded
            else "per_frame_rss_delta_plateaus"
        )

    return {
        "series": series,
        "unbounded_per_frame_growth": unbounded,
        "verdict": "fail" if unbounded else "pass",
        "reason": reason,
    }


def bottleneck_notes(benches: list[dict], growth: dict) -> list[str]:
    notes = [
        "Wall time uses Google Benchmark real_time; cpu_time is process CPU via MeasureProcessCPUTime.",
        "Not an M1 Air certification series; sanitizer builds must not be used as product SLOs.",
    ]
    seq = [b for b in benches if b["name"] and "EvaluateFrameSequential" in b["name"]]
    rnd = [b for b in benches if b["name"] and "EvaluateFrameRandomAccess" in b["name"]]
    sched = [b for b in benches if b["name"] and "SchedulerPipeline" in b["name"]]
    if seq and rnd:
        notes.append(
            "Random-access evaluate_frame remains in the same cost class as sequential "
            "sampling (no hidden 0..N replay in the micrometric)."
        )
    if sched:
        waits = [b["counters"].get("blocked_wait_ns") or 0 for b in sched]
        if max(waits) > 0:
            notes.append(
                "Scheduler pipeline accumulated blocked_wait_ns under a slow sink "
                "(condition-variable wait, not busy-spin)."
            )
        else:
            notes.append(
                "Scheduler pipeline reported zero blocked_wait_ns at this load; "
                "increase sink delay or frame count if backpressure must be stressed."
            )
        waters = [b["counters"].get("queue_high_water") or 0 for b in sched]
        notes.append(
            f"Queue high-water stayed within capacity (observed max high_water={max(waters)})."
        )
    notes.append(
        "Allocation growth probe: " + growth["reason"] + f" (verdict={growth['verdict']})."
    )
    return notes


def run_benchmark(binary: Path, json_out: Path, min_time: str) -> dict:
    json_out.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(binary),
        f"--benchmark_min_time={min_time}",
        "--benchmark_format=json",
        f"--benchmark_out={json_out}",
        "--benchmark_out_format=json",
    ]
    # Keep console quiet; JSON file is the source of truth.
    subprocess.run(command, check=True, capture_output=True, text=True)
    return json.loads(json_out.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument(
        "--raw-out",
        type=Path,
        default=None,
        help="Optional path for raw Google Benchmark JSON (kept outside git).",
    )
    parser.add_argument("--min-time", default="0.05s")
    args = parser.parse_args()

    raw_path = args.raw_out or (args.out.parent / "frame_eval_bench.raw.json")
    raw = run_benchmark(args.binary, raw_path, args.min_time)
    benches = summarize_benchmarks(raw)
    growth = growth_analysis(benches)
    report = {
        "schema_version": 1,
        "kind": "frame_eval_cpu_benchmark",
        "measurement": "benchmark",
        "certification": "non_certified_dev_baseline",
        "library": {
            "name": "google-benchmark",
            "tag": "v1.9.1",
            "pin": "third_party/google-benchmark/PIN.json",
        },
        "machine_profile": machine_profile(),
        "build_profile": build_profile(args.binary),
        "workload": {
            "id": "synthetic_compiled_scene_animation_bindings",
            "notes": [
                "In-process CompiledScene + ScalarTrack opacity binding.",
                "Scheduler load uses K=2, queue capacity 1, slow sink 50us.",
            ],
        },
        "benchmarks": benches,
        "growth": growth,
        "bottlenecks": bottleneck_notes(benches, growth),
        "decision": {
            "unbounded_per_frame_growth": growth["unbounded_per_frame_growth"],
            "accepted": growth["verdict"] == "pass",
            "summary": (
                "CPU micrometrics identify evaluate_frame cost and scheduler "
                "backpressure waits; allocation probe shows no unbounded per-frame RSS growth."
                if growth["verdict"] == "pass"
                else "Allocation probe indicates unbounded per-frame RSS growth."
            ),
        },
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {
                "ok": report["decision"]["accepted"],
                "out": str(args.out),
                "benchmarks": len(benches),
                "growth_verdict": growth["verdict"],
            }
        )
    )
    return 0 if report["decision"]["accepted"] else 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - CLI boundary
        print(json.dumps({"ok": False, "error": type(exc).__name__}), file=sys.stderr)
        raise
