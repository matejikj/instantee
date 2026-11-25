#!/usr/bin/env python3
"""Validate locked visual baselines for the three Style Bible workloads.

Fails when golden bytes drift without an approved change record, when the
signed review record is missing/rejected, or when readability checklist items
are not marked passed. Optionally enforces the Apple Silicon macOS baseline
platform declared in the registry.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_REGISTRY = REPO_ROOT / "contracts" / "visual-baseline" / "registry-v1.json"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def detect_platform() -> dict[str, str]:
    system = platform.system().lower()
    os_family = "macos" if system == "darwin" else system
    arch = platform.machine().lower()
    if arch in {"arm64", "aarch64"}:
        arch = "arm64"
    gpu_family = "unknown"
    if os_family == "macos" and arch == "arm64":
        gpu_family = "apple-silicon"
    return {"os_family": os_family, "arch": arch, "gpu_family": gpu_family}


def verify_platform(expected: dict, actual: dict) -> list[str]:
    errors: list[str] = []
    for key in ("os_family", "arch", "gpu_family"):
        if expected.get(key) != actual.get(key):
            errors.append(
                f"baseline platform mismatch for {key}: "
                f"expected {expected.get(key)!r}, got {actual.get(key)!r}"
            )
    return errors


def latest_change_digests(change_record: dict) -> dict[str, str]:
    latest: dict[str, str] = {}
    for entry in change_record.get("entries") or []:
        artifact_id = entry.get("artifact_id")
        next_sha = entry.get("next_sha256")
        if artifact_id and next_sha:
            latest[artifact_id] = next_sha
    return latest


def checklist_passed(review: dict, required_ids: list[str]) -> list[str]:
    results = {
        item.get("id"): item
        for item in (review.get("checklist_results") or [])
        if item.get("id")
    }
    errors: list[str] = []
    for item_id in required_ids:
        item = results.get(item_id)
        if item is None:
            errors.append(f"missing readability checklist result: {item_id}")
        elif not item.get("passed"):
            errors.append(f"readability checklist failed: {item_id}")
    return errors


def verify_style(repo_root: Path, style: dict, default_tolerances: dict) -> list[str]:
    style_id = style.get("id", "<unknown>")
    prefix = f"{style_id}: "
    errors: list[str] = []

    goldens_dir = repo_root / style["goldens_dir"]
    review_dir = repo_root / style["review_dir"]
    manifest_path = goldens_dir / "manifest.json"
    change_path = goldens_dir / "CHANGE_RECORD.json"
    review_path = review_dir / "review-record.json"
    tolerances_path = review_dir / "tolerances.json"

    for path in (manifest_path, change_path, review_path, tolerances_path):
        if not path.is_file():
            errors.append(prefix + f"missing {path.relative_to(repo_root).as_posix()}")
    if errors:
        return errors

    manifest = load_json(manifest_path)
    if manifest.get("workload") != style.get("workload"):
        errors.append(
            prefix
            + f"workload mismatch: registry {style.get('workload')!r} "
            + f"vs manifest {manifest.get('workload')!r}"
        )

    artifacts = manifest.get("artifacts") or []
    if not artifacts:
        errors.append(prefix + "manifest has no artifacts")

    changes = load_json(change_path)
    latest = latest_change_digests(changes)
    for artifact in artifacts:
        artifact_id = artifact.get("id")
        expected = artifact.get("sha256")
        kind = artifact.get("kind")
        if not artifact_id or not expected:
            errors.append(prefix + "artifact missing id/sha256")
            continue
        path = goldens_dir / artifact_id
        if not path.is_file():
            errors.append(prefix + f"missing artifact file {artifact_id}")
            continue
        actual = sha256_file(path)
        if actual != expected:
            errors.append(
                prefix
                + f"unreviewed or stale digest for {artifact_id} "
                + f"(manifest={expected}, bytes={actual})"
            )
        if latest.get(artifact_id) != expected:
            errors.append(
                prefix
                + f"CHANGE_RECORD does not approve current digest for {artifact_id}"
            )
        if kind == "png" and path.suffix.lower() != ".png":
            errors.append(prefix + f"png kind expected .png for {artifact_id}")

    review = load_json(review_path)
    if review.get("status") != "approved":
        errors.append(prefix + f"review status is {review.get('status')!r}, not approved")
    manifest_digest = sha256_file(manifest_path)
    if review.get("golden_manifest_sha256") != manifest_digest:
        errors.append(
            prefix
            + "review-record golden_manifest_sha256 does not match manifest.json "
            + "(update requires explicit approval)"
        )

    tolerances = load_json(tolerances_path)
    for key, value in default_tolerances.items():
        if tolerances.get(key) != value:
            errors.append(
                prefix
                + f"tolerance {key}={tolerances.get(key)!r} differs from registry "
                + f"default {value!r}"
            )
    review_tol = review.get("tolerances") or {}
    for key, value in default_tolerances.items():
        if review_tol.get(key) != value:
            errors.append(
                prefix
                + f"review-record tolerance {key}={review_tol.get(key)!r} differs "
                + f"from registry default {value!r}"
            )

    errors.extend(
        prefix + message
        for message in checklist_passed(
            review, list(style.get("readability_checklist_ids") or [])
        )
    )
    return errors


def verify_registry(
    registry_path: Path,
    *,
    repo_root: Path = REPO_ROOT,
    enforce_platform: bool = False,
) -> list[str]:
    registry = load_json(registry_path)
    errors: list[str] = []
    if enforce_platform:
        errors.extend(
            verify_platform(registry.get("baseline_platform") or {}, detect_platform())
        )
    defaults = registry.get("default_tolerances") or {}
    styles = registry.get("styles") or []
    if len(styles) < 3:
        errors.append("registry must list all three Style Bible baselines")
    for style in styles:
        errors.extend(verify_style(repo_root, style, defaults))
    return errors


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--registry",
        type=Path,
        default=DEFAULT_REGISTRY,
        help="visual baseline registry JSON",
    )
    parser.add_argument(
        "--repo",
        type=Path,
        default=REPO_ROOT,
        help="repository root",
    )
    parser.add_argument(
        "--enforce-platform",
        action="store_true",
        help="require the host OS/arch/GPU family to match the registry lock",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if not args.registry.is_file():
        print(f"missing registry: {args.registry}", file=sys.stderr)
        return 1
    errors = verify_registry(
        args.registry,
        repo_root=args.repo,
        enforce_platform=args.enforce_platform,
    )
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    try:
        display = args.registry.resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:
        display = args.registry.as_posix()
    print(f"visual baseline ok ({display})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
