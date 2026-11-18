#!/usr/bin/env python3
"""Pack Google Benchmark JSON for paper-curvature micrometrics.

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
                    for key in ("ops_used", "ops_budget", "rss_peak_bytes")
                    if counter_value(bench, key) is not None
                },
            }
        )
    return out


def tier_analysis(benches: list[dict]) -> dict:
    rows = []
    for bench in benches:
        ops = bench["counters"].get("ops_used")
        budget = bench["counters"].get("ops_budget")
        if ops is None or budget is None:
            continue
        rows.append(
            {
                "name": bench["name"],
                "ops_used": ops,
                "ops_budget": budget,
                "within_tier": ops <= budget,
            }
        )
    ok = bool(rows) and all(row["within_tier"] for row in rows)
    return {
        "tier_id": "paper-curvature-lite-v1",
        "rows": rows,
        "verdict": "pass" if ok else "fail",
        "reason": (
            "all_workloads_within_ops_budget"
            if ok
            else "missing_or_ops_exceed_budget"
        ),
    }


def bottleneck_notes(benches: list[dict], tier: dict) -> list[str]:
    return [
        "Wall time uses Google Benchmark real_time; cpu_time is process CPU.",
        "Not an M1 Air certification series; sanitizer builds must not be used as product SLOs.",
        "CPU reference deform mirrors the lite-tier vertex ALU budget for bend/wave.",
        "Tier check: " + tier["reason"] + f" (verdict={tier['verdict']}).",
        f"Benchmarks collected: {len(benches)}.",
    ]


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

    raw_path = args.raw_out or (args.out.parent / "paper_curvature_bench.raw.json")
    raw = run_benchmark(args.binary, raw_path, args.min_time)
    benches = summarize_benchmarks(raw)
    tier = tier_analysis(benches)
    report = {
        "schema_version": 1,
        "kind": "paper_curvature_benchmark",
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
            "id": "synthetic_paper_curvature_grid",
            "tier_id": "paper-curvature-lite-v1",
            "notes": [
                "In-process bend/wave deform of a regular paper grid.",
                "Counters report ops_used vs lite-tier ops_budget.",
            ],
        },
        "benchmarks": benches,
        "tier": tier,
        "bottlenecks": bottleneck_notes(benches, tier),
        "decision": {
            "within_complexity_tier": tier["verdict"] == "pass",
            "accepted": tier["verdict"] == "pass",
            "summary": (
                "Paper curvature bend/wave stay within paper-curvature-lite-v1 "
                "ops budget; silhouette integrity is covered by unit tests/goldens."
                if tier["verdict"] == "pass"
                else "Benchmark observed ops exceeding the lite complexity tier."
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
                "tier_verdict": tier["verdict"],
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
