#!/usr/bin/env python3
"""Validate the release versioning manifest against live repo sources.

Ensures framework_semver, scene_ir_schema_version, and style_package_version
stay separated and consistent with CMake, the TS package, Style Bible fixtures,
the schema compatibility matrix, and CHANGELOG release notes (including
Breaking Changes). Does not create git tags and makes no performance claims.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = (
    REPO_ROOT / "contracts" / "release" / "version-manifest-v1.json"
)
SEMVER = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def cmake_project_version(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    match = re.search(
        r"project\s*\(\s*Instantee\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
        text,
        flags=re.IGNORECASE,
    )
    if not match:
        raise ValueError(f"{path}: missing project(Instantee VERSION …)")
    return match.group(1)


def require_semver(label: str, value: object, errors: list[str]) -> None:
    if not isinstance(value, str) or not SEMVER.match(value):
        errors.append(f"{label} must be MAJOR.MINOR.PATCH, got {value!r}")


def validate_manifest(
    *,
    repo_root: Path,
    manifest_path: Path,
) -> list[str]:
    errors: list[str] = []
    if not manifest_path.is_file():
        return [f"missing manifest: {manifest_path}"]

    manifest = load_json(manifest_path)
    if manifest.get("schema_version") != 1:
        errors.append("manifest schema_version must be 1")

    release = manifest.get("release") or {}
    axes = manifest.get("axes") or {}
    matrix = manifest.get("compatibility_matrix") or {}
    process = manifest.get("process") or {}

    framework = (axes.get("framework_semver") or {}).get("value")
    require_semver("axes.framework_semver.value", framework, errors)
    if release.get("framework_semver") != framework:
        errors.append(
            "release.framework_semver must equal axes.framework_semver.value"
        )
    if release.get("git_tag") != f"v{framework}":
        errors.append(
            f"release.git_tag must be v{{framework_semver}}, "
            f"got {release.get('git_tag')!r}"
        )

    # framework_semver sources
    for source in (axes.get("framework_semver") or {}).get("sources") or []:
        rel = source.get("path")
        kind = source.get("kind")
        if not isinstance(rel, str):
            errors.append("framework source missing path")
            continue
        path = repo_root / rel
        if not path.is_file():
            errors.append(f"missing framework source: {rel}")
            continue
        try:
            if kind == "cmake_project_version":
                actual = cmake_project_version(path)
            elif kind == "npm_package_version":
                actual = load_json(path).get("version")
            else:
                errors.append(f"unknown framework source kind: {kind!r}")
                continue
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            errors.append(f"{rel}: {exc}")
            continue
        if actual != framework:
            errors.append(
                f"{rel}: version {actual!r} != framework_semver {framework!r}"
            )

    # scene IR + authoring from schema matrix
    schema_matrix_path = repo_root / str(
        matrix.get("schema_matrix_path")
        or "contracts/compatibility/schema-version-matrix-v1.json"
    )
    if not schema_matrix_path.is_file():
        errors.append(f"missing schema matrix: {schema_matrix_path}")
        return errors

    schema_matrix = load_json(schema_matrix_path)
    families = schema_matrix.get("families") or {}

    scene_axis = axes.get("scene_ir_schema_version") or {}
    scene_value = scene_axis.get("value")
    require_semver("axes.scene_ir_schema_version.value", scene_value, errors)
    family_scene = families.get("scene-ir") or {}
    if family_scene.get("current") != scene_value:
        errors.append(
            "scene_ir_schema_version must match schema matrix scene-ir.current "
            f"({family_scene.get('current')!r} vs {scene_value!r})"
        )

    authoring_axis = axes.get("authoring_schema_version") or {}
    authoring_value = authoring_axis.get("value")
    require_semver("axes.authoring_schema_version.value", authoring_value, errors)
    family_authoring = families.get("authoring") or {}
    if family_authoring.get("current") != authoring_value:
        errors.append(
            "authoring_schema_version must match schema matrix authoring.current "
            f"({family_authoring.get('current')!r} vs {authoring_value!r})"
        )

    # style packages
    style_axis = axes.get("style_package_version") or {}
    packages = style_axis.get("packages") or []
    if len(packages) < 3:
        errors.append("style_package_version.packages must list the three V1 styles")
    seen_ids: set[str] = set()
    for package in packages:
        pkg_id = package.get("id")
        version = package.get("version")
        fixture_rel = package.get("fixture")
        require_semver(f"style {pkg_id} version", version, errors)
        if pkg_id in seen_ids:
            errors.append(f"duplicate style package id: {pkg_id!r}")
        seen_ids.add(str(pkg_id))
        if not isinstance(fixture_rel, str):
            errors.append(f"style {pkg_id}: missing fixture path")
            continue
        fixture = repo_root / fixture_rel
        if not fixture.is_file():
            errors.append(f"missing style fixture: {fixture_rel}")
            continue
        doc = load_json(fixture)
        if doc.get("package_id") != pkg_id:
            errors.append(
                f"{fixture_rel}: package_id {doc.get('package_id')!r} "
                f"!= manifest {pkg_id!r}"
            )
        if doc.get("version") != version:
            errors.append(
                f"{fixture_rel}: version {doc.get('version')!r} "
                f"!= manifest {version!r}"
            )

    supports = matrix.get("supports") or {}
    if supports.get("scene_ir_schema_version") != [scene_value]:
        errors.append(
            "compatibility_matrix.supports.scene_ir_schema_version "
            "must equal [scene_ir_schema_version]"
        )
    if authoring_value not in (supports.get("authoring_schema_version") or []):
        errors.append(
            "compatibility_matrix.supports.authoring_schema_version "
            "must include current authoring schema"
        )

    # migrations must match schema matrix migratable entries
    expected_migrations: list[dict[str, str]] = []
    for family_name, family in families.items():
        for entry in family.get("supported") or []:
            if entry.get("status") != "migratable":
                continue
            expected_migrations.append(
                {
                    "family": family_name,
                    "from": str(entry.get("version")),
                    "to": str(family.get("current")),
                    "migrator": str(entry.get("migrator")),
                }
            )
    declared = matrix.get("migrations") or []
    if len(declared) != len(expected_migrations):
        errors.append(
            "compatibility_matrix.migrations count must match schema matrix "
            f"migratable entries ({len(declared)} vs {len(expected_migrations)})"
        )
    for expected in expected_migrations:
        match = next(
            (
                item
                for item in declared
                if item.get("family") == expected["family"]
                and item.get("from") == expected["from"]
                and item.get("to") == expected["to"]
                and item.get("migrator") == expected["migrator"]
            ),
            None,
        )
        if match is None:
            errors.append(
                "missing migration declaration for "
                f"{expected['family']} {expected['from']}→{expected['to']}"
            )
        else:
            fixture = match.get("fixture")
            if isinstance(fixture, str) and not (repo_root / fixture).exists():
                errors.append(f"migration fixture missing: {fixture}")

    # rejected lists must include matrix rejected_examples for key families
    rejected = matrix.get("rejected_without_migration") or {}
    for family_name in ("authoring", "scene-ir", "style-bible"):
        expected = set((families.get(family_name) or {}).get("rejected_examples") or [])
        actual = set(rejected.get(family_name) or [])
        if not expected.issubset(actual):
            errors.append(
                f"rejected_without_migration.{family_name} missing "
                f"{sorted(expected - actual)}"
            )

    # changelog / release notes
    changelog_rel = release.get("changelog_path") or "CHANGELOG.md"
    changelog_path = repo_root / str(changelog_rel)
    if not changelog_path.is_file():
        errors.append(f"missing changelog: {changelog_rel}")
        return errors
    changelog = changelog_path.read_text(encoding="utf-8")
    heading = process.get("changelog_required_heading") or release.get(
        "release_notes_heading"
    )
    if not isinstance(heading, str) or heading not in changelog:
        errors.append(f"changelog missing required heading {heading!r}")
    for section in process.get("changelog_required_sections") or []:
        # Section must appear after the release heading.
        if not isinstance(heading, str) or heading not in changelog:
            break
        after = changelog.split(heading, 1)[1]
        # Stop at next top-level ## heading if present.
        next_release = re.search(r"\n## \[", after)
        body = after if next_release is None else after[: next_release.start()]
        if section not in body:
            errors.append(
                f"changelog release notes missing section {section!r} under {heading}"
            )

    # Axes must remain distinct values conceptually even when equal numerically:
    # require the three primary axis keys to exist as separate objects.
    for key in (
        "framework_semver",
        "scene_ir_schema_version",
        "style_package_version",
    ):
        if key not in axes or not isinstance(axes[key], dict):
            errors.append(f"axes.{key} must be a distinct object")

    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    errors = validate_manifest(
        repo_root=args.repo_root.resolve(),
        manifest_path=args.manifest.resolve(),
    )
    report = {
        "schema_version": 1,
        "ok": not errors,
        "error_count": len(errors),
        "errors": errors,
        "certified_performance": False,
    }
    if args.json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    elif errors:
        print("release version manifest check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
    else:
        print("release version manifest ok")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
