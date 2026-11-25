#!/usr/bin/env python3
"""Release install smoke: use a staged macOS package without compiling Metal.

Copies the package to a temporary prefix, runs CLI help and renders the bundled
smoke Scene IR. Refuses to invoke cmake. Not a performance certification run.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


def run(command: list[str], *, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    if any(part == "cmake" or part.endswith("/cmake") for part in command):
        raise RuntimeError("install smoke must not invoke cmake")
    return subprocess.run(
        command, cwd=cwd, text=True, capture_output=True, check=False
    )


def looks_like_mp4(path: Path) -> bool:
    return b"ftyp" in path.read_bytes()[:64]


def smoke(*, package_dir: Path, work_dir: Path) -> dict:
    package_dir = package_dir.resolve()
    work_dir = work_dir.resolve()
    if not package_dir.is_dir():
        raise RuntimeError(f"missing package dir: {package_dir}")
    cli = package_dir / "bin" / "instantee"
    scene = package_dir / "share" / "instantee" / "smoke" / "smoke.scene.json"
    policy_path = package_dir / "POLICY.json"
    manifest_path = package_dir / "MANIFEST.json"
    for required in (cli, scene, policy_path, manifest_path):
        if not required.is_file():
            raise RuntimeError(f"package missing {required.name}")

    if work_dir.exists():
        shutil.rmtree(work_dir)
    shutil.copytree(package_dir, work_dir)

    work_cli = work_dir / "bin" / "instantee"
    work_scene = work_dir / "share" / "instantee" / "smoke" / "smoke.scene.json"
    out_mp4 = work_dir / "smoke.mp4"

    help_result = run([str(work_cli), "help"], cwd=work_dir)
    if help_result.returncode != 0:
        raise RuntimeError(
            f"help failed: {help_result.stdout}\n{help_result.stderr}"
        )

    render = run(
        [
            str(work_cli),
            "render",
            str(work_scene),
            "--out",
            str(out_mp4),
            "--frames",
            "3",
            "--overwrite",
        ],
        cwd=work_dir,
    )
    if render.returncode != 0:
        raise RuntimeError(
            f"render failed ({render.returncode}): {render.stdout}\n{render.stderr}"
        )
    if not out_mp4.is_file() or not looks_like_mp4(out_mp4):
        raise RuntimeError("smoke MP4 missing or not an MP4 container")

    policy = json.loads(policy_path.read_text(encoding="utf-8"))
    return {
        "schema_version": 1,
        "ok": True,
        "certified_performance": False,
        "compiled_renderer_on_host": False,
        "invoked_cmake": False,
        "package_name": policy.get("package_name"),
        "deployment_target": policy.get("platform", {}).get("deployment_target"),
        "signing_mode": policy.get("signing", {}).get("mode"),
        "video_bytes": out_mp4.stat().st_size,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-dir", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--write-report", type=Path, default=None)
    args = parser.parse_args(argv)

    try:
        report = smoke(package_dir=args.package_dir, work_dir=args.work_dir)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        print("macos install smoke failed", file=sys.stderr)
        return 1

    if args.write_report is not None:
        args.write_report.parent.mkdir(parents=True, exist_ok=True)
        args.write_report.write_text(
            json.dumps(report, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
    print(
        "macos install smoke ok "
        f"(package={report.get('package_name')}, "
        f"cmake=false, video_bytes={report['video_bytes']})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
