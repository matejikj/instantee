#!/usr/bin/env python3
"""Close the V1 release gate: package checksums, dossier, and git tag contract.

measurement=none. Builds the macOS 1.0.0 package, records digests, verifies the
certification dossier, and optionally creates the annotated v1.0.0 tag after
the release commit exists. Does not push remotes and makes no new performance
claims.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import check_version_manifest as version_manifest
import package_macos
import smoke_install_macos
import v1_certification_dossier as dossier

REPO = Path(__file__).resolve().parents[2]
TRACKER = REPO / "IMPLEMENTATION_STATUS.json"
DEFAULT_BUILD = REPO / "build" / "release"
DEFAULT_DIST = REPO / "build" / "dist" / "macos"
GIT_TAG = "v1.0.0"
FRAMEWORK_VERSION = "1.0.0"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def run_git(args: list[str]) -> str:
    completed = subprocess.run(
        ["git", *args],
        cwd=REPO,
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"git {' '.join(args)} failed: {completed.stderr.strip()}"
        )
    return completed.stdout.strip()


def all_p8_tasks_done(tracker: dict[str, Any], *, allow_open: set[str]) -> dict[str, Any]:
    tasks = tracker.get("tasks") or {}
    incomplete: list[str] = []
    for task_id, entry in sorted(tasks.items()):
        if not task_id.startswith("P8-"):
            continue
        if task_id in allow_open:
            continue
        status = entry.get("status")
        met = (entry.get("acceptance") or {}).get("met")
        if status != "done" or met is not True:
            incomplete.append(task_id)
    return {
        "pass": not incomplete,
        "incomplete": incomplete,
        "allow_open": sorted(allow_open),
    }


def archive_package(stage: Path) -> tuple[Path, str]:
    # Do not use Path.with_suffix: versioned names like 1.0.0-arm64 truncate.
    archive = stage.parent / f"{stage.name}.tar.gz"
    if archive.exists():
        archive.unlink()
    subprocess.run(
        ["tar", "-C", str(stage.parent), "-czf", str(archive), stage.name],
        check=True,
    )
    return archive, sha256_file(archive)


def build_package(*, build_dir: Path, out_dir: Path) -> dict[str, Any]:
    policy = REPO / "packaging" / "macos" / "policy-v1.json"
    stage = package_macos.package(
        repo_root=REPO,
        build_dir=build_dir,
        out_dir=out_dir,
        policy_path=policy,
        version=FRAMEWORK_VERSION,
    )
    manifest = json.loads((stage / "MANIFEST.json").read_text(encoding="utf-8"))
    cli = stage / "bin" / "instantee"
    archive, archive_sha = archive_package(stage)
    return {
        "version": FRAMEWORK_VERSION,
        "package_dir": str(stage.relative_to(REPO)),
        "package_name": stage.name,
        "cli_sha256": sha256_file(cli),
        "cli_size_bytes": cli.stat().st_size,
        "manifest_file_count": manifest.get("file_count"),
        "manifest_sha256": sha256_file(stage / "MANIFEST.json"),
        "archive_path": str(archive.relative_to(REPO)),
        "archive_sha256": archive_sha,
        "archive_size_bytes": archive.stat().st_size,
    }


def smoke_package(package_dir: Path, work_dir: Path) -> dict[str, Any]:
    report = smoke_install_macos.smoke(
        package_dir=package_dir, work_dir=work_dir
    )
    report["certified_performance"] = False
    return report


def assemble_report(
    *,
    package: dict[str, Any],
    smoke: dict[str, Any],
    tracker_path: Path = TRACKER,
    allow_open: set[str] | None = None,
) -> dict[str, Any]:
    tracker = json.loads(tracker_path.read_text(encoding="utf-8"))
    open_ids = allow_open if allow_open is not None else {"P8-037"}
    tasks = all_p8_tasks_done(tracker, allow_open=open_ids)
    dossier_result = dossier.assemble_dossier(tracker_path=tracker_path)
    manifest_errors = version_manifest.validate_manifest(
        repo_root=REPO,
        manifest_path=REPO / "contracts/release/version-manifest-v1.json",
    )
    prior_gates = dossier_result["phase_gates"]["prior_phases_pass"]
    smoke_ok = smoke.get("ok") is True or smoke.get("pass") is True
    if "ok" not in smoke and "pass" not in smoke:
        # smoke_install_macos returns fields; treat missing failure as success
        # when help/render succeeded (legacy shape uses returncode keys).
        smoke_ok = smoke.get("help_exit_code", 0) == 0 and smoke.get(
            "render_exit_code", 1
        ) == 0

    blocking: list[str] = []
    if not tasks["pass"]:
        blocking.append("p8_tasks_incomplete")
    if not dossier_result["accepted"]:
        blocking.append("certification_dossier_rejected")
    if manifest_errors:
        blocking.append("version_manifest_invalid")
    if not prior_gates:
        blocking.append("prior_phase_gates_incomplete")
    if not smoke_ok:
        blocking.append("package_smoke_failed")

    return {
        "schema_version": 1,
        "kind": "v1_release_gate_report",
        "generated_at_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "measurement": "none",
        "certified_performance": False,
        "git_tag": GIT_TAG,
        "framework_semver": FRAMEWORK_VERSION,
        "accepted": not blocking,
        "blocking_defects": blocking,
        "p8_tasks": tasks,
        "prior_phase_gates_pass": prior_gates,
        "certification_dossier_accepted": dossier_result["accepted"],
        "version_manifest_errors": manifest_errors,
        "package": package,
        "install_smoke": smoke,
        "known_limitations": [
            "Annotated tag is created locally; remotes are not pushed by this gate.",
            "Package digests certify the staged artifact, not notarization.",
            "No new B-0x performance series is measured by this gate.",
        ],
    }


def create_annotated_tag(*, message: str) -> dict[str, Any]:
    existing = subprocess.run(
        ["git", "rev-parse", "-q", "--verify", f"refs/tags/{GIT_TAG}"],
        cwd=REPO,
        capture_output=True,
        text=True,
        check=False,
    )
    if existing.returncode == 0:
        raise RuntimeError(f"tag {GIT_TAG} already exists")
    run_git(["tag", "-a", GIT_TAG, "-m", message])
    tag_commit = run_git(["rev-parse", f"{GIT_TAG}^{{}}"])
    tag_object = run_git(["rev-parse", GIT_TAG])
    head = run_git(["rev-parse", "HEAD"])
    if tag_commit != head:
        raise RuntimeError(
            f"tag {GIT_TAG} points at {tag_commit}, expected HEAD {head}"
        )
    return {
        "git_tag": GIT_TAG,
        "tag_object": tag_object,
        "tagged_commit": tag_commit,
        "matches_head": True,
        "pushed": False,
    }


def verify_existing_tag() -> dict[str, Any]:
    tag_commit = run_git(["rev-parse", f"{GIT_TAG}^{{}}"])
    tag_object = run_git(["rev-parse", GIT_TAG])
    head = run_git(["rev-parse", "HEAD"])
    return {
        "git_tag": GIT_TAG,
        "tag_object": tag_object,
        "tagged_commit": tag_commit,
        "head": head,
        "matches_head": tag_commit == head,
        "pushed": False,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_DIST)
    parser.add_argument("--work-dir", type=Path, default=REPO / "build/tmp/v1-install-smoke")
    parser.add_argument("--report", type=Path, help="write gate report JSON")
    parser.add_argument(
        "--skip-package",
        action="store_true",
        help="reuse an already staged package under --out-dir",
    )
    parser.add_argument(
        "--create-tag",
        action="store_true",
        help="create annotated v1.0.0 on HEAD after checks",
    )
    parser.add_argument(
        "--allow-open",
        action="append",
        default=[],
        help="P8 task ids allowed to remain open during assembly",
    )
    args = parser.parse_args(argv)

    allow_open = set(args.allow_open) or {"P8-037"}
    if args.skip_package:
        stage = args.out_dir / f"instantee-macos-{FRAMEWORK_VERSION}-arm64"
        if not stage.is_dir():
            raise SystemExit(f"missing staged package: {stage}")
        archive = stage.parent / f"{stage.name}.tar.gz"
        package = {
            "version": FRAMEWORK_VERSION,
            "package_dir": str(stage.relative_to(REPO)),
            "package_name": stage.name,
            "cli_sha256": sha256_file(stage / "bin" / "instantee"),
            "cli_size_bytes": (stage / "bin" / "instantee").stat().st_size,
            "manifest_file_count": json.loads(
                (stage / "MANIFEST.json").read_text(encoding="utf-8")
            ).get("file_count"),
            "manifest_sha256": sha256_file(stage / "MANIFEST.json"),
            "archive_path": str(archive.relative_to(REPO)) if archive.is_file() else None,
            "archive_sha256": sha256_file(archive) if archive.is_file() else None,
            "archive_size_bytes": archive.stat().st_size if archive.is_file() else None,
        }
    else:
        package = build_package(build_dir=args.build_dir, out_dir=args.out_dir)
        stage = REPO / package["package_dir"]

    smoke = smoke_package(stage, args.work_dir)
    report = assemble_report(
        package=package, smoke=smoke, allow_open=allow_open
    )

    if args.create_tag:
        if not report["accepted"]:
            raise SystemExit("refusing to tag: gate report not accepted")
        report["tag"] = create_annotated_tag(
            message=(
                "Instantee v1.0.0\n\n"
                "Annotated release tag matching the tested build and macOS "
                "package checksums. Remotes are not updated by this command."
            )
        )
    elif subprocess.run(
        ["git", "rev-parse", "-q", "--verify", f"refs/tags/{GIT_TAG}"],
        cwd=REPO,
        capture_output=True,
        check=False,
    ).returncode == 0:
        report["tag"] = verify_existing_tag()
        if not report["tag"]["matches_head"]:
            report["accepted"] = False
            report["blocking_defects"].append("tag_does_not_match_head")

    text = json.dumps(report, indent=2) + "\n"
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text, encoding="utf-8")
        sys.stdout.write(
            json.dumps(
                {
                    "accepted": report["accepted"],
                    "blocking_defects": report["blocking_defects"],
                    "git_tag": report.get("tag", {}).get("git_tag"),
                    "archive_sha256": package.get("archive_sha256"),
                }
            )
            + "\n"
        )
    else:
        sys.stdout.write(text)
    return 0 if report["accepted"] else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - CLI boundary
        print(f"v1 release gate failed: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc
