#!/usr/bin/env python3
"""Pack Google Benchmark JSON for instance-batching micrometrics.

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
                    for key in ("source_ops", "draw_calls", "rss_peak_bytes")
                    if counter_value(bench, key) is not None
                },
            }
        )
    return out


def reduction_analysis(benches: list[dict]) -> dict:
    rows = []
    for bench in benches:
        if not bench["name"] or "BatchPassPlan" not in bench["name"]:
            continue
        source = bench["counters"].get("source_ops")
        draws = bench["counters"].get("draw_calls")
        if source is None or draws is None or source <= 0:
            continue
        rows.append(
            {
                "name": bench["name"],
                "source_ops": source,
                "draw_calls": draws,
                "reduction_ratio": source / max(draws, 1.0),
            }
        )
    ok = bool(rows) and all(row["draw_calls"] < row["source_ops"] for row in rows)
    return {
        "rows": rows,
        "verdict": "pass" if ok else "fail",
        "reason": (
            "batched_draw_calls_below_source_ops"
            if ok
            else "missing_or_no_draw_call_reduction"
        ),
    }


def bottleneck_notes(benches: list[dict], reduction: dict) -> list[str]:
    notes = [
        "Wall time uses Google Benchmark real_time; cpu_time is process CPU.",
        "Not an M1 Air certification series; sanitizer builds must not be used as product SLOs.",
        "Instance batching coalesces consecutive ops that share texture_binding; "
        "transparent order is preserved inside each batch.",
        "Draw-call reduction: " + reduction["reason"]
        + f" (verdict={reduction['verdict']}).",
    ]
    raster = [b for b in benches if b["name"] and "RasterizeBatched" in b["name"]]
    if raster:
        notes.append(
            "CPU rasterize_batched_pass micrometric exercises expand+blend after planning."
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
    subprocess.run(command, check=True, capture_output=True, text=True)
    return json.loads(json_out.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--raw-out", type=Path, default=None)
    parser.add_argument("--min-time", default="0.05s")
    args = parser.parse_args()

    raw_path = args.raw_out or (args.out.parent / "instance_batch_bench.raw.json")
    raw = run_benchmark(args.binary, raw_path, args.min_time)
    benches = summarize_benchmarks(raw)
    reduction = reduction_analysis(benches)
    report = {
        "schema_version": 1,
        "kind": "instance_batch_benchmark",
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
            "id": "synthetic_fill_rect_instance_grid",
            "notes": [
                "In-process PassDesc of fill_rect ops with optional texture_binding groups.",
                "Counters report source_ops vs coalesced draw_calls.",
            ],
        },
        "benchmarks": benches,
        "reduction": reduction,
        "bottlenecks": bottleneck_notes(benches, reduction),
        "decision": {
            "draw_call_reduction_observed": reduction["verdict"] == "pass",
            "accepted": reduction["verdict"] == "pass",
            "summary": (
                "Instance batching reduces draw calls below source op count for "
                "compatible consecutive shapes; CPU goldens cover pixel identity."
                if reduction["verdict"] == "pass"
                else "Benchmark did not observe draw-call reduction."
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
                "reduction_verdict": reduction["verdict"],
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
