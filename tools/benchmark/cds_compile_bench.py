#!/usr/bin/env python3
"""Benchmark CDS compile stages across three Style Bible skeletons.

Drives release `instantee validate --style-bible` for 60s/300s authoring under
editorial, paper-cinema, and scientific-notebook. Produces an anonymized JSON
report that separates CDS from parse/compile. Results are development
baselines, not M1 Air certification claims.
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


STYLES = (
    {
        "id": "editorial",
        "authoring_style": "editorial@1",
        "bible": "contracts/fixtures/style-bible/positive/editorial.style.json",
    },
    {
        "id": "paper-cinema",
        "authoring_style": "paper-cinema@1",
        "bible": "contracts/fixtures/style-bible/positive/paper-cinema.style.json",
    },
    {
        "id": "scientific-notebook",
        "authoring_style": "scientific-notebook@1",
        "bible": (
            "contracts/fixtures/style-bible/positive/"
            "scientific-notebook.style.json"
        ),
    },
)

# Draft NFR-PERF-03 ceilings from ADR-0014 (non-certified). Exceedances are
# recorded as risks, not silent PASS.
DRAFT_BUDGETS = {
    "typical_60s": {"wall_p95_ms": 50.0, "rss_peak_bytes": 32 * 1024 * 1024},
    "typical_300s": {"wall_p95_ms": 100.0, "rss_peak_bytes": 64 * 1024 * 1024},
}


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


def shot(index: int, duration_frames: int) -> dict:
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
                "text": f"HEADLINE {index}",
                "role": "headline",
                "visible": True,
                "transform": {
                    "translate": {"x": 10, "y": 20, "z": 0},
                    "opacity": 1,
                },
            }
        ],
    }


def build_manifest(style: str, duration_frames: int) -> dict:
    shot_duration = 30
    count = max(1, duration_frames // shot_duration)
    transitions = []
    for index in range(count - 1):
        transitions.append(
            {
                "id": f"tr{index}",
                "from_shot_id": f"shot{index}",
                "to_shot_id": f"shot{index + 1}",
                "blend": "dissolve",
                "overlap_frames": 6,
            }
        )
    return {
        "schema_version": "1.0.0",
        "film_id": "cds-bench-film",
        "canvas": {"width": 1080, "height": 1920, "color": "rec709-sdr"},
        "frame_rate": {"numerator": 30, "denominator": 1},
        "seed": 42,
        "style": style,
        "assets": [],
        "rigs": [],
        "transitions": transitions,
        "shots": [shot(i, shot_duration) for i in range(count)],
    }


def write_manifest(path: Path, doc: dict) -> int:
    data = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    path.write_bytes(data)
    return len(data)


def run_validate(binary: Path, manifest: Path, style_bible: Path) -> dict:
    start = time.monotonic_ns()
    process = subprocess.Popen(
        [str(binary), "validate", str(manifest), "--style-bible", str(style_bible)],
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
            f"validate failed exit={exit_code} stderr={stderr.decode('utf-8', 'replace')[:300]}"
        )
    report = json.loads(stdout.decode("utf-8"))
    stages = report["stages_ms"]
    if "cds" not in stages:
        raise RuntimeError("validate report missing stages_ms.cds breakdown")
    return {
        "wall_ms": (end - start) / 1e6,
        "stages_ms": stages,
        "ir_bytes": report["ir_bytes"],
        "duration_frames": report["duration_frames"],
        "rss_peak_bytes": int(usage.ru_maxrss) if usage.ru_maxrss > 0 else None,
    }


def pack_samples(samples: list[dict]) -> dict:
    cds_keys = (
        "style_resolve",
        "shot_planning",
        "transition_planning",
        "lint",
        "direction_report",
        "total",
    )
    packed = {
        "wall_ms": summarize([s["wall_ms"] for s in samples]),
        "parse_ms": summarize([s["stages_ms"]["parse"] for s in samples]),
        "compile_ms": summarize([s["stages_ms"]["compile"] for s in samples]),
        "canonicalize_ms": summarize(
            [s["stages_ms"]["canonicalize"] for s in samples]
        ),
        "write_ms": summarize([s["stages_ms"].get("write", 0.0) for s in samples]),
        "cds": {
            key: summarize([s["stages_ms"]["cds"][key] for s in samples])
            for key in cds_keys
        },
        "ir_bytes": samples[0]["ir_bytes"] if samples else None,
        "rss_peak_bytes": {
            "sample_count": len(samples),
            "min": min(s["rss_peak_bytes"] for s in samples)
            if samples and samples[0]["rss_peak_bytes"] is not None
            else None,
            "p50": nearest_rank([float(s["rss_peak_bytes"]) for s in samples], 50)
            if samples and samples[0]["rss_peak_bytes"] is not None
            else None,
            "p95": nearest_rank([float(s["rss_peak_bytes"]) for s in samples], 95)
            if samples and samples[0]["rss_peak_bytes"] is not None
            else None,
            "max": max(s["rss_peak_bytes"] for s in samples)
            if samples and samples[0]["rss_peak_bytes"] is not None
            else None,
            "unit": "bytes",
            "source": "Darwin wait4 ru_maxrss",
        },
    }
    return packed


def measure_case(
    binary: Path,
    *,
    style_id: str,
    authoring_style: str,
    bible: Path,
    class_name: str,
    duration_frames: int,
    repeats: int,
    warm_extra: int,
) -> dict:
    with tempfile.TemporaryDirectory(prefix="instantee-cds-compile-") as tmp:
        manifest = Path(tmp) / f"{style_id}-{class_name}.authoring.json"
        doc = build_manifest(authoring_style, duration_frames)
        input_bytes = write_manifest(manifest, doc)
        cold_samples = [
            run_validate(binary, manifest, bible) for _ in range(repeats)
        ]
        warm_samples = [
            run_validate(binary, manifest, bible) for _ in range(warm_extra)
        ]
        return {
            "style": style_id,
            "class": class_name,
            "input_bytes": input_bytes,
            "duration_frames": cold_samples[0]["duration_frames"],
            "transition_count": len(doc["transitions"]),
            "cold": pack_samples(cold_samples),
            "warm": pack_samples(warm_samples),
        }


def evaluate_budgets(cases: list[dict]) -> list[dict]:
    risks: list[dict] = []
    for case in cases:
        budget = DRAFT_BUDGETS.get(case["class"])
        if budget is None:
            continue
        wall_p95 = case["cold"]["wall_ms"]["p95"]
        rss_p95 = case["cold"]["rss_peak_bytes"]["p95"]
        exceeded = []
        if wall_p95 is not None and wall_p95 > budget["wall_p95_ms"]:
            exceeded.append(
                {
                    "metric": "cold_wall_p95_ms",
                    "observed": wall_p95,
                    "draft_ceiling": budget["wall_p95_ms"],
                }
            )
        if rss_p95 is not None and rss_p95 > budget["rss_peak_bytes"]:
            exceeded.append(
                {
                    "metric": "cold_rss_peak_p95_bytes",
                    "observed": rss_p95,
                    "draft_ceiling": budget["rss_peak_bytes"],
                }
            )
        status = "exceeds_draft_budget" if exceeded else "within_draft_budget"
        entry = {
            "style": case["style"],
            "class": case["class"],
            "status": status,
            "exceedances": exceeded,
            "cds_share_of_compile_p50": None,
        }
        compile_p50 = case["cold"]["compile_ms"]["p50"]
        cds_p50 = case["cold"]["cds"]["total"]["p50"]
        if compile_p50 and compile_p50 > 0 and cds_p50 is not None:
            entry["cds_share_of_compile_p50"] = cds_p50 / compile_p50
        if exceeded:
            risks.append(entry)
        else:
            risks.append(entry)
    return risks


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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--warm-extra", type=int, default=5)
    args = parser.parse_args()

    classes = [
        ("typical_60s", 1800),
        ("typical_300s", 9000),
    ]

    results = []
    for style in STYLES:
        bible = args.source_root / style["bible"]
        if not bible.is_file():
            raise FileNotFoundError(f"missing style bible: {bible}")
        for class_name, duration in classes:
            results.append(
                measure_case(
                    args.binary,
                    style_id=style["id"],
                    authoring_style=style["authoring_style"],
                    bible=bible,
                    class_name=class_name,
                    duration_frames=duration,
                    repeats=args.repeats,
                    warm_extra=args.warm_extra,
                )
            )

    budget_eval = evaluate_budgets(results)
    risks = [row for row in budget_eval if row["status"] == "exceeds_draft_budget"]

    report = {
        "schema_version": 1,
        "kind": "cds_compile_benchmark",
        "measurement": "benchmark",
        "certification": "non_certified_dev_baseline",
        "notes": [
            "CDS stages are a breakdown inside compile_ms; parse/canonicalize/write stay separate.",
            "Cold/warm are process-local series; warm means filesystem-warm new processes.",
            "Draft budget exceedances are recorded as risks, never as silent PASS.",
            "Not an M1 Air certification series.",
        ],
        "draft_budgets": DRAFT_BUDGETS,
        "machine_profile": machine_profile(),
        "binary": "instantee",
        "cases": results,
        "budget_evaluation": budget_eval,
        "risks": risks,
        "decision": {
            "cds_separated_from_parse_compile": True,
            "draft_budget_exceedance_is_risk": True,
            "pass_implies_within_draft": len(risks) == 0,
        },
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {
                "ok": True,
                "out": str(args.out),
                "cases": len(results),
                "risks": len(risks),
            }
        )
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - CLI boundary
        print(json.dumps({"ok": False, "error": type(exc).__name__}), file=sys.stderr)
        raise
