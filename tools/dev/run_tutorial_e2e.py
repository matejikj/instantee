#!/usr/bin/env python3
"""Executable docs test: build three tutorial MP4s from the public SDK + CLI.

Follows tutorials/README.md. Writes authoring JSON via the tutorial Node
samples, compiles with Style Bible fixtures, and renders MP4 outputs. Short
`--frames` values are for smoke runs and are not certified performance claims.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

TUTORIALS = (
    {
        "id": "editorial",
        "script": "authoring/examples/tutorials/editorial/main.mjs",
        "style_bible": "contracts/fixtures/style-bible/positive/editorial.style.json",
    },
    {
        "id": "notebook",
        "script": "authoring/examples/tutorials/notebook/main.mjs",
        "style_bible": (
            "contracts/fixtures/style-bible/positive/scientific-notebook.style.json"
        ),
    },
    {
        "id": "paper-cinema",
        "script": "authoring/examples/tutorials/paper-cinema/main.mjs",
        "style_bible": "contracts/fixtures/style-bible/positive/paper-cinema.style.json",
    },
)


def run(command: list[str], *, cwd: Path) -> None:
    completed = subprocess.run(
        command, cwd=cwd, text=True, capture_output=True, check=False
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"command failed ({completed.returncode}): {' '.join(command)}\n"
            f"{completed.stdout}\n{completed.stderr}"
        )


def looks_like_mp4(path: Path) -> bool:
    data = path.read_bytes()[:64]
    return b"ftyp" in data


def run_tutorials(
    *,
    repo_root: Path,
    cli: Path,
    work_dir: Path,
    frames: int | None,
) -> dict:
    work_dir.mkdir(parents=True, exist_ok=True)
    videos: list[dict] = []
    for item in TUTORIALS:
        style_id = item["id"]
        authoring = work_dir / f"{style_id}.authoring.json"
        scene = work_dir / f"{style_id}.scene.json"
        video = work_dir / f"{style_id}.mp4"
        run(
            [
                "node",
                str(repo_root / item["script"]),
                "--out",
                str(authoring),
            ],
            cwd=repo_root,
        )
        if not authoring.is_file() or authoring.stat().st_size == 0:
            raise RuntimeError(f"missing authoring output for {style_id}")
        # CLI requires --out before optional --style-bible.
        run(
            [
                str(cli),
                "compile",
                str(authoring),
                "--out",
                str(scene),
                "--style-bible",
                str(repo_root / item["style_bible"]),
            ],
            cwd=repo_root,
        )
        render_cmd = [
            str(cli),
            "render",
            str(scene),
            "--out",
            str(video),
            "--overwrite",
        ]
        if frames is not None:
            render_cmd.extend(["--frames", str(frames)])
        run(render_cmd, cwd=repo_root)
        if not video.is_file() or video.stat().st_size == 0:
            raise RuntimeError(f"missing video for {style_id}")
        if not looks_like_mp4(video):
            raise RuntimeError(f"output is not an MP4 container: {video}")
        videos.append(
            {
                "id": style_id,
                "path": video.as_posix(),
                "bytes": video.stat().st_size,
            }
        )
    return {
        "schema_version": 1,
        "ok": True,
        "certified_performance": False,
        "work_dir": work_dir.as_posix(),
        "videos": videos,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--cli",
        type=Path,
        default=REPO_ROOT / "build" / "release" / "instantee",
        help="path to instantee binary",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        required=True,
        help="directory for authoring/scene/mp4 outputs",
    )
    parser.add_argument(
        "--frames",
        type=int,
        default=None,
        help="optional render frame cap for smoke runs",
    )
    parser.add_argument(
        "--write-report",
        type=Path,
        default=None,
        help="optional JSON report path",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=REPO_ROOT,
    )
    args = parser.parse_args(argv)

    if not args.cli.is_file():
        print(f"missing CLI binary: {args.cli}", file=sys.stderr)
        return 1
    for item in TUTORIALS:
        script = args.repo_root / item["script"]
        bible = args.repo_root / item["style_bible"]
        if not script.is_file():
            print(f"missing tutorial script: {script}", file=sys.stderr)
            return 1
        if not bible.is_file():
            print(f"missing style bible: {bible}", file=sys.stderr)
            return 1

    try:
        report = run_tutorials(
            repo_root=args.repo_root,
            cli=args.cli,
            work_dir=args.work_dir,
            frames=args.frames,
        )
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        print("tutorial e2e failed", file=sys.stderr)
        return 1

    body = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.write_report is not None:
        args.write_report.parent.mkdir(parents=True, exist_ok=True)
        # Anonymize absolute work_dir to a placeholder for committed evidence.
        anonymized = json.loads(body)
        anonymized["work_dir"] = "<work-dir>"
        for video in anonymized["videos"]:
            name = Path(video["path"]).name
            video["path"] = f"<work-dir>/{name}"
        args.write_report.write_text(
            json.dumps(anonymized, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )

    ids = ", ".join(v["id"] for v in report["videos"])
    print(f"tutorial e2e ok ({ids})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
