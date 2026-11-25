#!/usr/bin/env python3
"""Export the three publishable V1 reference films via the Instantee CLI.

Renders each locked Scene IR to a silent 1080×1920 / 30 fps MP4. Optionally
writes digests back into the catalog. Not a certified performance run.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

import probe_mp4 as probe

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CATALOG = (
    REPO_ROOT / "contracts" / "release" / "reference-films" / "catalog-v1.json"
)


def run_render(*, cli: Path, scene: Path, out_mp4: Path, frames: int) -> None:
    out_mp4.parent.mkdir(parents=True, exist_ok=True)
    if out_mp4.exists():
        out_mp4.unlink()
    completed = subprocess.run(
        [
            str(cli),
            "render",
            str(scene),
            "--out",
            str(out_mp4),
            "--frames",
            str(frames),
            "--overwrite",
        ],
        cwd=str(REPO_ROOT),
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"render failed for {scene.name} ({completed.returncode}): "
            f"{completed.stdout}\n{completed.stderr}"
        )
    if not out_mp4.is_file():
        raise RuntimeError(f"missing output after render: {out_mp4.name}")


def export_films(
    *,
    repo_root: Path,
    catalog_path: Path,
    cli: Path,
    out_dir: Path,
    write_digests: bool,
) -> dict:
    catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    requirements = catalog["technical_requirements"]
    frames = int(requirements["duration_frames"])
    probes: list[dict] = []
    errors: list[str] = []

    for film in catalog["films"]:
        scene = repo_root / film["scene"]
        style = repo_root / film["style_bible"]
        review = repo_root / film["visual_review_record"]
        for required in (scene, style, review):
            if not required.is_file():
                raise RuntimeError(f"missing {required.as_posix()}")
        scene_doc = json.loads(scene.read_text(encoding="utf-8"))
        packages = (scene_doc.get("provenance") or {}).get("style_packages") or []
        expected = film["style_package"]
        if not any(
            p.get("id") == expected["id"] and p.get("version") == expected["version"]
            for p in packages
        ):
            raise RuntimeError(
                f"{scene.name}: provenance style_packages missing {expected}"
            )

        out_mp4 = out_dir / film["filename"]
        run_render(cli=cli, scene=scene, out_mp4=out_mp4, frames=frames)
        result = probe.probe_mp4(out_mp4)
        probes.append(result)
        errors.extend(
            probe.validate_probe(
                result,
                requirements,
                expected_sha256=None,
            )
        )
        film["sha256"] = result["sha256"]

    if errors:
        raise RuntimeError("; ".join(errors))

    if write_digests:
        catalog_path.write_text(
            json.dumps(catalog, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )

    return {
        "schema_version": 1,
        "ok": True,
        "film_count": len(probes),
        "out_dirname": out_dir.name,
        "probes": probes,
        "digests_written": write_digests,
        "certified_performance": False,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument(
        "--cli",
        type=Path,
        default=REPO_ROOT / "build" / "release" / "instantee",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=REPO_ROOT / "build" / "dist" / "reference-films",
    )
    parser.add_argument(
        "--write-digests",
        action="store_true",
        help="update catalog-v1.json sha256 fields after successful probes",
    )
    parser.add_argument("--write-report", type=Path, default=None)
    args = parser.parse_args(argv)

    try:
        report = export_films(
            repo_root=args.repo_root.resolve(),
            catalog_path=args.catalog.resolve(),
            cli=args.cli.resolve(),
            out_dir=args.out_dir.resolve(),
            write_digests=args.write_digests,
        )
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        print("reference film export failed", file=sys.stderr)
        return 1

    if args.write_report is not None:
        args.write_report.parent.mkdir(parents=True, exist_ok=True)
        args.write_report.write_text(
            json.dumps(report, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
    print(
        "reference film export ok "
        f"(films={report['film_count']}, digests_written={report['digests_written']})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
