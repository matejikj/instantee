#!/usr/bin/env python3
"""Audit pinned dependency manifests for unexpected native or Node packages."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

# Certification baseline documented in tools/toolchain/README.md.
EXPECTED_VCPKG_BASELINE = "74e6536215718009aae747d86d84b78376bf9e09"
ALLOWED_ROOT_DEV_DEPENDENCIES = frozenset(
    {
        "ajv",
        "ajv-formats",
        "json-schema-to-typescript",
        "typescript",
    }
)


def audit_vcpkg(manifest: dict) -> list[str]:
    errors: list[str] = []
    baseline = manifest.get("builtin-baseline")
    if baseline != EXPECTED_VCPKG_BASELINE:
        errors.append(
            f"vcpkg builtin-baseline drift: expected {EXPECTED_VCPKG_BASELINE}, got {baseline!r}"
        )
    dependencies = manifest.get("dependencies")
    if dependencies != []:
        errors.append(
            "vcpkg dependencies must stay empty until the first package is integrated "
            f"in manifest mode; found {dependencies!r}"
        )
    return errors


def audit_package_json(manifest: dict) -> list[str]:
    errors: list[str] = []
    if "dependencies" in manifest and manifest["dependencies"]:
        errors.append(
            "root package.json must not declare runtime dependencies; "
            f"found {sorted(manifest['dependencies'])}"
        )
    dev = manifest.get("devDependencies") or {}
    unexpected = sorted(set(dev) - ALLOWED_ROOT_DEV_DEPENDENCIES)
    if unexpected:
        errors.append(
            "unexpected root package.json devDependencies: " + ", ".join(unexpected)
        )
    missing = sorted(ALLOWED_ROOT_DEV_DEPENDENCIES - set(dev))
    if missing:
        errors.append(
            "missing expected root package.json devDependencies: " + ", ".join(missing)
        )
    return errors


def run_audit(repo_root: Path = REPO_ROOT) -> list[str]:
    errors: list[str] = []
    vcpkg_path = repo_root / "vcpkg.json"
    package_path = repo_root / "package.json"
    lock_path = repo_root / "package-lock.json"
    if not vcpkg_path.is_file():
        errors.append("missing vcpkg.json")
    else:
        errors.extend(audit_vcpkg(json.loads(vcpkg_path.read_text(encoding="utf-8"))))
    if not package_path.is_file():
        errors.append("missing package.json")
    else:
        errors.extend(
            audit_package_json(json.loads(package_path.read_text(encoding="utf-8")))
        )
    if not lock_path.is_file():
        errors.append("missing package-lock.json")
    return errors


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)
    errors = run_audit()
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("dependency audit passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
