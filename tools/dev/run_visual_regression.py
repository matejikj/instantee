#!/usr/bin/env python3
"""Run the three-style visual regression gate and write CI artifacts.

Combines the locked baseline/approval check with the Style Bible visual
acceptance CTests. Durations are correctness diagnostics only.
"""

from __future__ import annotations

import argparse
import datetime
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_REGISTRY = REPO_ROOT / "contracts" / "visual-baseline" / "registry-v1.json"


def anonymize(text: str) -> str:
    return text.replace(str(REPO_ROOT), "<repo>")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def run(argv: list[str]) -> subprocess.CompletedProcess[str]:
    print("==>", " ".join(argv), flush=True)
    return subprocess.run(argv, cwd=REPO_ROOT, text=True, capture_output=True)


def ctest_names(registry_path: Path) -> list[str]:
    document = json.loads(registry_path.read_text(encoding="utf-8"))
    names = [
        style["ctest_name"]
        for style in document.get("styles") or []
        if style.get("ctest_name")
    ]
    if len(names) != 3:
        raise SystemExit("registry must declare exactly three style ctest names")
    return names


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-p",
        "--build-dir",
        type=Path,
        required=True,
        help="configured CMake build directory",
    )
    parser.add_argument(
        "--artifact-dir",
        type=Path,
        required=True,
        help="directory for anonymized CI artifacts",
    )
    parser.add_argument(
        "--registry",
        type=Path,
        default=DEFAULT_REGISTRY,
        help="visual baseline registry JSON",
    )
    parser.add_argument(
        "--enforce-platform",
        action="store_true",
        help="require host platform to match the baseline lock",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    artifact_dir = args.artifact_dir
    if not artifact_dir.is_absolute():
        artifact_dir = (REPO_ROOT / artifact_dir).resolve()
    artifact_dir.mkdir(parents=True, exist_ok=True)

    names = ctest_names(args.registry)
    regex = "^(" + "|".join(names) + ")$"

    try:
        registry_display = args.registry.resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:
        registry_display = args.registry.as_posix()
    try:
        build_display = args.build_dir.resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:
        build_display = args.build_dir.as_posix()

    baseline_cmd = [
        "python3",
        "tools/dev/check_visual_baseline.py",
        "--registry",
        registry_display,
    ]
    if args.enforce_platform:
        baseline_cmd.append("--enforce-platform")
    baseline_run = [
        "python3",
        "tools/dev/check_visual_baseline.py",
        "--registry",
        str(args.registry),
    ]
    if args.enforce_platform:
        baseline_run.append("--enforce-platform")

    steps: list[dict] = []
    failed = False
    for name, run_command, report_command in (
        ("baseline-approval", tuple(baseline_run), tuple(baseline_cmd)),
        (
            "ctest-visual-acceptance",
            (
                "ctest",
                "--test-dir",
                str(args.build_dir),
                "--output-on-failure",
                "--no-tests=error",
                "-R",
                regex,
            ),
            (
                "ctest",
                "--test-dir",
                build_display,
                "--output-on-failure",
                "--no-tests=error",
                "-R",
                regex,
            ),
        ),
    ):
        result = run(list(run_command))
        log_name = f"{name}.log"
        write_text(
            artifact_dir / log_name,
            anonymize((result.stdout or "") + (result.stderr or "")),
        )
        record = {
            "name": name,
            "command": list(report_command),
            "exit_code": result.returncode,
            "status": "passed" if result.returncode == 0 else "failed",
            "log": log_name,
        }
        steps.append(record)
        if result.returncode != 0:
            failed = True

    summary = {
        "schema_version": 1,
        "generated_at_utc": datetime.datetime.now(datetime.UTC).strftime(
            "%Y-%m-%dT%H:%M:%SZ"
        ),
        "profile": "visual-regression-correctness",
        "certified_performance": False,
        "notes": [
            "Three-style golden/approval gate with readability checklist enforcement.",
            "Do not treat durations from this profile as release certification timings.",
        ],
        "build_dir": build_display,
        "styles": names,
        "steps": steps,
        "result": "failed" if failed else "passed",
    }
    write_text(artifact_dir / "summary.json", json.dumps(summary, indent=2) + "\n")
    print(f"visual-regression result: {summary['result']}", flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
