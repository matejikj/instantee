#!/usr/bin/env python3
"""Pack Google Benchmark JSON for texture-atlas micrometrics.

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
                    for key in (
                        "insert_hits",
                        "insert_misses",
                        "hit_rate",
                        "staging_bytes",
                        "warm_staging_bytes",
                        "rss_peak_bytes",
                    )
                    if counter_value(bench, key) is not None
                },
            }
        )
    return out


def atlas_analysis(benches: list[dict]) -> dict:
    rows = []
    for bench in benches:
        if not bench["name"] or "AtlasPackAndUpload" not in bench["name"]:
            continue
        staging = bench["counters"].get("staging_bytes")
        warm = bench["counters"].get("warm_staging_bytes")
        hits = bench["counters"].get("insert_hits")
        misses = bench["counters"].get("insert_misses")
        if staging is None or warm is None or hits is None or misses is None:
            continue
        rows.append(
            {
                "name": bench["name"],
                "staging_bytes": staging,
                "warm_staging_bytes": warm,
                "insert_hits": hits,
                "insert_misses": misses,
                "hit_rate": bench["counters"].get("hit_rate"),
            }
        )
    ok = bool(rows) and all(
        row["staging_bytes"] > 0 and row["warm_staging_bytes"] == 0 and row["insert_hits"] > 0
        for row in rows
    )
    return {
        "rows": rows,
        "verdict": "pass" if ok else "fail",
        "reason": (
            "cold_staging_nonzero_warm_zero_with_hits"
            if ok
            else "missing_hit_rate_or_warm_restage"
        ),
    }


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

    raw_path = args.raw_out or (args.out.parent / "texture_atlas_bench.raw.json")
    raw = run_benchmark(args.binary, raw_path, args.min_time)
    benches = summarize_benchmarks(raw)
    analysis = atlas_analysis(benches)
    report = {
        "schema_version": 1,
        "kind": "texture_atlas_benchmark",
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
            "id": "synthetic_rgba_sprite_atlas",
            "notes": [
                "In-process shelf packing of solid RGBA sprites with 1px padding.",
                "Counters report insert hit rate and cold/warm staging bytes.",
            ],
        },
        "benchmarks": benches,
        "atlas": analysis,
        "bottlenecks": [
            "Wall time uses Google Benchmark real_time; cpu_time is process CPU.",
            "Not an M1 Air certification series; sanitizer builds must not be used as product SLOs.",
            "Warm staging_bytes==0 means resident sprites are not reinstantiated each frame.",
            "Atlas analysis: " + analysis["reason"] + f" (verdict={analysis['verdict']}).",
        ],
        "decision": {
            "hit_rate_and_staging_reported": analysis["verdict"] == "pass",
            "accepted": analysis["verdict"] == "pass",
            "summary": (
                "Texture atlas reports hit rate and stages dirty sprites once; "
                "warm plans upload zero bytes."
                if analysis["verdict"] == "pass"
                else "Benchmark did not report usable hit rate / staging counters."
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
                "atlas_verdict": analysis["verdict"],
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
