#!/usr/bin/env python3
"""Run the macOS GPU/shader integration gate and write CI artifacts.

This profile validates offline MSL pins, CPU/GPU layout contracts, Metal
goldens, resource retirement and a short video path. Timings are correctness
only and must not be published as certified release performance.
"""

from __future__ import annotations

import argparse
import datetime
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


def run(argv: list[str], *, cwd: Path = REPO_ROOT) -> subprocess.CompletedProcess[str]:
    print("==>", " ".join(argv), flush=True)
    return subprocess.run(argv, cwd=cwd, text=True, capture_output=True)


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def anonymize(text: str) -> str:
    """Replace absolute repo paths with a stable placeholder for CI artifacts."""
    return text.replace(str(REPO_ROOT), "<repo>")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-p",
        "--build-dir",
        type=Path,
        required=True,
        help="configured CMake build directory (for example build/dev)",
    )
    parser.add_argument(
        "--artifact-dir",
        type=Path,
        required=True,
        help="directory for anonymized CI artifacts",
    )
    parser.add_argument(
        "--ctest-label",
        default="gpu",
        help="CTest label selecting the GPU integration suite",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    artifact_dir = args.artifact_dir
    if not artifact_dir.is_absolute():
        artifact_dir = (REPO_ROOT / artifact_dir).resolve()
    artifact_dir.mkdir(parents=True, exist_ok=True)

    steps: list[dict] = []
    failed = False

    for name, command in (
        (
            "shader-pin",
            ("python3", "tools/dev/check_shader_pin.py"),
        ),
        (
            "shader-layouts",
            ("python3", "tools/dev/check_shader_layouts.py"),
        ),
        (
            "ctest-gpu",
            (
                "ctest",
                "--test-dir",
                str(args.build_dir),
                "--output-on-failure",
                "--no-tests=error",
                "-L",
                args.ctest_label,
            ),
        ),
    ):
        result = run(list(command))
        log_name = f"{name}.log"
        write_text(
            artifact_dir / log_name,
            anonymize((result.stdout or "") + (result.stderr or "")),
        )
        record = {
            "name": name,
            "command": list(command),
            "exit_code": result.returncode,
            "status": "passed" if result.returncode == 0 else "failed",
            "log": log_name,
        }
        steps.append(record)
        if result.returncode != 0:
            failed = True
            # Keep going so artifacts capture later failures too.

    summary = {
        "schema_version": 1,
        "generated_at_utc": datetime.datetime.now(datetime.UTC).strftime(
            "%Y-%m-%dT%H:%M:%SZ"
        ),
        "profile": "gpu-integration-correctness",
        "certified_performance": False,
        "notes": [
            "Offline MSL pin + CPU/GPU layout + Metal golden/retirement/video stress.",
            "Do not treat durations from this profile as release certification timings.",
        ],
        "build_dir": (
            args.build_dir.resolve().relative_to(REPO_ROOT).as_posix()
            if args.build_dir.resolve().is_relative_to(REPO_ROOT)
            else args.build_dir.as_posix()
        ),
        "ctest_label": args.ctest_label,
        "steps": steps,
        "result": "failed" if failed else "passed",
    }
    write_text(artifact_dir / "summary.json", json.dumps(summary, indent=2) + "\n")
    print(f"gpu-integration result: {summary['result']}", flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
