#!/usr/bin/env python3
"""Validate publishable reference-film catalog, creative signoff, and MP4 probes.

With --require-files, each catalogued MP4 must exist under --out-dir, match the
recorded sha256, and satisfy 1080×1920 / 30 fps / silent technical requirements.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

import probe_mp4 as probe

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CATALOG = (
    REPO_ROOT / "contracts" / "release" / "reference-films" / "catalog-v1.json"
)
DEFAULT_SIGNOFF = (
    REPO_ROOT
    / "contracts"
    / "release"
    / "reference-films"
    / "creative-signoff-v1.json"
)

REQUIRED_SIGNOFF_ITEMS = {
    "visual_direction",
    "tempo",
    "transitions",
    "phone_text_readability",
    "style_bible_match",
    "silent_vertical_mp4",
}


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def validate_signoff(
    *,
    repo_root: Path,
    signoff: dict[str, Any],
    catalog: dict[str, Any],
) -> list[str]:
    errors: list[str] = []
    if signoff.get("schema_version") != 1:
        errors.append("signoff schema_version must be 1")
    if signoff.get("status") != "approved":
        errors.append("creative signoff status must be approved")
    if not signoff.get("approved_by_role"):
        errors.append("creative signoff missing approved_by_role")
    if not signoff.get("approved_at_utc"):
        errors.append("creative signoff missing approved_at_utc")

    results = {
        item.get("id"): item for item in (signoff.get("checklist_results") or [])
    }
    missing = REQUIRED_SIGNOFF_ITEMS - set(results)
    if missing:
        errors.append(f"signoff missing checklist items: {sorted(missing)}")
    for item_id, item in results.items():
        if item_id in REQUIRED_SIGNOFF_ITEMS and not item.get("passed"):
            errors.append(f"signoff checklist item failed: {item_id}")

    catalog_ids = [film["id"] for film in catalog.get("films") or []]
    signoff_films = signoff.get("films") or []
    signoff_ids = [film.get("id") for film in signoff_films]
    if signoff_ids != catalog_ids:
        errors.append("signoff films must match catalog film ids in order")

    for film in catalog.get("films") or []:
        review_path = repo_root / film["visual_review_record"]
        if not review_path.is_file():
            errors.append(f"missing visual review: {film['visual_review_record']}")
            continue
        review = load_json(review_path)
        if review.get("status") != "approved":
            errors.append(
                f"{film['id']}: visual review status is {review.get('status')!r}"
            )
        style_path = repo_root / film["style_bible"]
        if not style_path.is_file():
            errors.append(f"missing style bible: {film['style_bible']}")
            continue
        style = load_json(style_path)
        expected = film["style_package"]
        if style.get("package_id") != expected["id"] or style.get("version") != expected[
            "version"
        ]:
            errors.append(
                f"{film['id']}: style bible identity mismatch "
                f"({style.get('package_id')}@{style.get('version')})"
            )
        scene_path = repo_root / film["scene"]
        if not scene_path.is_file():
            errors.append(f"missing scene: {film['scene']}")
            continue
        scene = load_json(scene_path)
        packages = (scene.get("provenance") or {}).get("style_packages") or []
        if not any(
            p.get("id") == expected["id"] and p.get("version") == expected["version"]
            for p in packages
        ):
            errors.append(f"{film['id']}: scene provenance missing style package")
    return errors


def validate(
    *,
    repo_root: Path,
    catalog_path: Path,
    signoff_path: Path,
    out_dir: Path | None,
    require_files: bool,
) -> list[str]:
    errors: list[str] = []
    if not catalog_path.is_file():
        return [f"missing catalog: {catalog_path}"]
    if not signoff_path.is_file():
        return [f"missing signoff: {signoff_path}"]

    catalog = load_json(catalog_path)
    signoff = load_json(signoff_path)
    if catalog.get("schema_version") != 1:
        errors.append("catalog schema_version must be 1")
    films = catalog.get("films") or []
    if len(films) != 3:
        errors.append("catalog must list exactly three reference films")

    errors.extend(
        validate_signoff(repo_root=repo_root, signoff=signoff, catalog=catalog)
    )

    requirements = catalog.get("technical_requirements") or {}
    for film in films:
        digest = film.get("sha256")
        if require_files:
            if not isinstance(digest, str) or len(digest) != 64:
                errors.append(f"{film.get('id')}: catalog sha256 required")
                continue
            if out_dir is None:
                errors.append("--out-dir required with --require-files")
                continue
            path = out_dir / film["filename"]
            if not path.is_file():
                errors.append(f"missing mp4: {film['filename']}")
                continue
            try:
                result = probe.probe_mp4(path)
            except RuntimeError as exc:
                errors.append(str(exc))
                continue
            errors.extend(
                probe.validate_probe(
                    result, requirements, expected_sha256=digest
                )
            )
        elif digest is not None and not (
            isinstance(digest, str) and len(digest) == 64
        ):
            errors.append(f"{film.get('id')}: sha256 must be null or 64 hex chars")
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--signoff", type=Path, default=DEFAULT_SIGNOFF)
    parser.add_argument("--out-dir", type=Path, default=None)
    parser.add_argument(
        "--require-files",
        action="store_true",
        help="require exported MP4s under --out-dir matching catalog digests",
    )
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    errors = validate(
        repo_root=args.repo_root.resolve(),
        catalog_path=args.catalog.resolve(),
        signoff_path=args.signoff.resolve(),
        out_dir=None if args.out_dir is None else args.out_dir.resolve(),
        require_files=args.require_files,
    )
    report = {
        "schema_version": 1,
        "ok": not errors,
        "error_count": len(errors),
        "errors": errors,
        "require_files": args.require_files,
        "certified_performance": False,
    }
    if args.json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    elif errors:
        print("reference films check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
    else:
        mode = "with files" if args.require_files else "catalog+signoff"
        print(f"reference films check ok ({mode})")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
