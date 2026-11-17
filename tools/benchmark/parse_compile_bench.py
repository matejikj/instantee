#!/usr/bin/env python3
"""Benchmark CLI validate stage timings for three authoring size classes.

Produces an anonymized JSON report (no hostnames, user paths, or device UUIDs).
Results are development baselines, not M1 Air certification claims.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import resource
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def nearest_rank(values: list[float], percent: int) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = (len(ordered) * percent + 99) // 100 - 1
    return ordered[max(0, min(index, len(ordered) - 1))]


def summarize(values: list[float]) -> dict:
    return {
        "sample_count": len(values),
        "min": min(values) if values else None,
        "p50": nearest_rank(values, 50),
        "p95": nearest_rank(values, 95),
        "max": max(values) if values else None,
        "unit": "ms",
        "method": "exact_nearest_rank",
    }


def shot(index: int, duration_frames: int, text_pad: int = 0) -> dict:
    text = "TECH HAS CHANGED" + ("x" * text_pad)
    return {
        "id": f"shot{index}",
        "duration_frames": duration_frames,
        "intent": {
            "type": "reveal",
            "intensity": 0.5,
            "importance": 0.5,
            "target_ids": [f"n{index}"],
        },
        "nodes": [
            {
                "id": f"n{index}",
                "type": "text",
                "text": text,
                "role": "headline",
                "visible": True,
                "transform": {
                    "translate": {"x": 10, "y": 20, "z": 0},
                    "opacity": 1,
                },
            }
        ],
    }


def build_manifest(duration_frames: int, *, target_bytes: int | None = None) -> dict:
    # Keep shot count modest; pad node text when approaching the byte ceiling.
    shot_duration = 30
    count = max(1, duration_frames // shot_duration)
    pad = 0
    while True:
        doc = {
            "schema_version": "1.0.0",
            "film_id": "bench-film",
            "canvas": {"width": 1080, "height": 1920, "color": "rec709-sdr"},
            "frame_rate": {"numerator": 30, "denominator": 1},
            "seed": 42,
            "style": "editorial@1",
            "assets": [],
            "rigs": [],
            "transitions": [],
            "shots": [shot(i, shot_duration, pad) for i in range(count)],
        }
        encoded = json.dumps(doc, separators=(",", ":")).encode("utf-8")
        if target_bytes is None or len(encoded) >= target_bytes or pad > 200_000:
            return doc
        remaining = target_bytes - len(encoded)
        pad += max(64, remaining // max(1, count) + 64)


def write_manifest(path: Path, doc: dict) -> int:
    data = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    path.write_bytes(data)
    return len(data)


def run_validate(binary: Path, manifest: Path) -> dict:
    start = time.monotonic_ns()
    process = subprocess.Popen(
        [str(binary), "validate", str(manifest)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )
    pid, status, usage = os.wait4(process.pid, 0)
    end = time.monotonic_ns()
    assert pid == process.pid
    stdout = process.stdout.read() if process.stdout else b""
    stderr = process.stderr.read() if process.stderr else b""
    exit_code = os.waitstatus_to_exitcode(status)
    if exit_code != 0:
        raise RuntimeError(
            f"validate failed exit={exit_code} stderr={stderr.decode('utf-8', 'replace')[:200]}"
        )
    report = json.loads(stdout.decode("utf-8"))
    return {
        "wall_ms": (end - start) / 1e6,
        "stages_ms": report["stages_ms"],
        "ir_bytes": report["ir_bytes"],
        "duration_frames": report["duration_frames"],
        "rss_peak_bytes": int(usage.ru_maxrss) if usage.ru_maxrss > 0 else None,
        "user_ns": int(round(usage.ru_utime * 1e6)) * 1000,
        "system_ns": int(round(usage.ru_stime * 1e6)) * 1000,
    }


def measure_class(
    binary: Path, name: str, doc: dict, repeats: int, warm_extra: int
) -> dict:
    with tempfile.TemporaryDirectory(prefix="instantee-parse-compile-") as tmp:
        manifest = Path(tmp) / f"{name}.authoring.json"
        input_bytes = write_manifest(manifest, doc)
        cold_samples = []
        for _ in range(repeats):
            cold_samples.append(run_validate(binary, manifest))
        # Warm: same file, new processes after the cold series (filesystem cache warm).
        warm_samples = []
        for _ in range(warm_extra):
            warm_samples.append(run_validate(binary, manifest))

        def pack(samples: list[dict]) -> dict:
            return {
                "wall_ms": summarize([s["wall_ms"] for s in samples]),
                "parse_ms": summarize([s["stages_ms"]["parse"] for s in samples]),
                "structural_ms": summarize(
                    [s["stages_ms"].get("structural", 0.0) for s in samples]
                ),
                "compile_ms": summarize([s["stages_ms"]["compile"] for s in samples]),
                "graph_ms": summarize(
                    [s["stages_ms"].get("graph", 0.0) for s in samples]
                ),
                "canonicalize_ms": summarize(
                    [s["stages_ms"]["canonicalize"] for s in samples]
                ),
                "write_ms": summarize(
                    [s["stages_ms"].get("write", 0.0) for s in samples]
                ),
                "ir_bytes": samples[0]["ir_bytes"] if samples else None,
                "rss_peak_bytes": {
                    "sample_count": len(samples),
                    "min": min(s["rss_peak_bytes"] for s in samples)
                    if samples and samples[0]["rss_peak_bytes"] is not None
                    else None,
                    "p50": nearest_rank(
                        [float(s["rss_peak_bytes"]) for s in samples], 50
                    )
                    if samples and samples[0]["rss_peak_bytes"] is not None
                    else None,
                    "p95": nearest_rank(
                        [float(s["rss_peak_bytes"]) for s in samples], 95
                    )
                    if samples and samples[0]["rss_peak_bytes"] is not None
                    else None,
                    "max": max(s["rss_peak_bytes"] for s in samples)
                    if samples and samples[0]["rss_peak_bytes"] is not None
                    else None,
                    "unit": "bytes",
                    "source": "Darwin wait4 ru_maxrss",
                },
            }

        return {
            "class": name,
            "input_bytes": input_bytes,
            "duration_frames": cold_samples[0]["duration_frames"],
            "cold": pack(cold_samples),
            "warm": pack(warm_samples),
        }


def machine_profile() -> dict:
    # Allowlisted technical fields only; no hostname, serial, or user path.
    return {
        "os": platform.system(),
        "os_release": platform.release(),
        "arch": platform.machine(),
        "python": platform.python_version(),
        "logical_cpus": os.cpu_count(),
        "page_size": resource.getpagesize(),
        "certification": "non_certified_dev_baseline",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--warm-extra", type=int, default=5)
    args = parser.parse_args()

    # Security upper bound from scene-ir LoadLimits::max_bytes (16 MiB).
    max_bytes = 16 * 1024 * 1024
    classes = [
        ("typical_60s", build_manifest(1800)),
        ("typical_300s", build_manifest(9000)),
        (
            "security_limit",
            build_manifest(1800, target_bytes=max_bytes - 64 * 1024),
        ),
    ]

    results = []
    for name, doc in classes:
        results.append(
            measure_class(args.binary, name, doc, args.repeats, args.warm_extra)
        )

    report = {
        "schema_version": 1,
        "kind": "parse_compile_benchmark",
        "measurement": "benchmark",
        "certification": "non_certified_dev_baseline",
        "notes": [
            "Wall and stage times are process-local; not an M1 Air certification series.",
            "Warm means filesystem-warm new processes after cold samples of the same file.",
            "security_limit targets just under LoadLimits::max_bytes (16 MiB).",
        ],
        "machine_profile": machine_profile(),
        "binary": "instantee",
        "classes": results,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"ok": True, "out": str(args.out), "classes": len(results)}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - CLI boundary
        print(json.dumps({"ok": False, "error": type(exc).__name__}), file=sys.stderr)
        raise
