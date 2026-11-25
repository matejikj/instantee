#!/usr/bin/env python3
"""Assemble a relocatable macOS CLI distribution package.

Stages the release CLI (embedded metallibs), contracts, fonts, authoring SDK
surface and a precompiled smoke Scene IR. Applies the declared ad-hoc signing
policy and writes MANIFEST/POLICY metadata. Does not notarize and does not
claim certified performance.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_POLICY = REPO_ROOT / "packaging" / "macos" / "policy-v1.json"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def run(command: list[str], *, cwd: Path | None = None) -> None:
    completed = subprocess.run(
        command, cwd=cwd, text=True, capture_output=True, check=False
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"command failed ({completed.returncode}): {' '.join(command)}\n"
            f"{completed.stdout}\n{completed.stderr}"
        )


def probe_binary(cli: Path) -> dict:
    info: dict[str, object] = {
        "path": "bin/instantee",
        "size_bytes": cli.stat().st_size,
        "sha256": sha256_file(cli),
    }
    # Deployment target / SDK from otool (technical fields only).
    completed = subprocess.run(
        ["otool", "-l", str(cli)], capture_output=True, text=True, check=False
    )
    if completed.returncode == 0:
        minos = None
        sdk = None
        for line in completed.stdout.splitlines():
            stripped = line.strip()
            if stripped.startswith("minos "):
                minos = stripped.split()[-1]
            if stripped.startswith("sdk "):
                sdk = stripped.split()[-1]
        info["minos"] = minos
        info["sdk"] = sdk
    completed = subprocess.run(
        ["otool", "-L", str(cli)], capture_output=True, text=True, check=False
    )
    libs: list[str] = []
    if completed.returncode == 0:
        for line in completed.stdout.splitlines()[1:]:
            lib = line.strip().split(" ", 1)[0]
            if lib:
                libs.append(lib)
    info["linked_libraries"] = libs
    # Non-system dylibs would require packaging; V1 expects only system paths.
    non_system = [
        lib
        for lib in libs
        if not (
            lib.startswith("/usr/lib/")
            or lib.startswith("/System/Library/")
            or lib.startswith("@")
        )
    ]
    info["non_system_libraries"] = non_system
    completed = subprocess.run(
        ["otool", "-l", str(cli)], capture_output=True, text=True, check=False
    )
    rpaths: list[str] = []
    if completed.returncode == 0:
        lines = completed.stdout.splitlines()
        for index, line in enumerate(lines):
            if "LC_RPATH" in line:
                for follow in lines[index : index + 4]:
                    if "path " in follow:
                        rpaths.append(follow.strip().split("path ", 1)[1].split(" ", 1)[0])
    info["rpaths"] = rpaths
    return info


def adhoc_sign(cli: Path) -> dict:
    run(["codesign", "--force", "--sign", "-", "--timestamp=none", str(cli)])
    completed = subprocess.run(
        ["codesign", "-dv", "--verbose=2", str(cli)],
        capture_output=True,
        text=True,
        check=False,
    )
    # codesign -dv writes to stderr
    text = completed.stderr + completed.stdout
    identity = "ad-hoc" if "Signature=adhoc" in text or "flags=0x2(adhoc)" in text or "Authority=(adhoc)" in text else "unknown"
    if "Signature=adhoc" in text or "flags=0x2(adhoc)" in text:
        identity = "ad-hoc"
    return {"mode": "ad-hoc", "codesign_ok": completed.returncode == 0, "identity": identity}


def write_package_readme(path: Path, version: str) -> None:
    path.write_text(
        f"""# Instantee macOS package {version}

Relocatable CLI distribution (arm64). Metallibs are embedded in `bin/instantee`.

## Quick smoke

```sh
./bin/instantee help
./bin/instantee render share/instantee/smoke/smoke.scene.json \\
  --out /tmp/instantee-smoke.mp4 --frames 3 --overwrite
```

This package does not require compiling the Metal renderer on the install host.
See `POLICY.json` for deployment target, RPATH and signing policy.
""",
        encoding="utf-8",
    )


def build_manifest(root: Path) -> dict:
    files: list[dict[str, str]] = []
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        if rel in {"MANIFEST.json"}:
            continue
        files.append({"path": rel, "sha256": sha256_file(path)})
    return {"schema_version": 1, "files": files, "file_count": len(files)}


def package(
    *,
    repo_root: Path,
    build_dir: Path,
    out_dir: Path,
    policy_path: Path,
    version: str,
) -> Path:
    repo_root = repo_root.resolve()
    build_dir = build_dir.resolve()
    out_dir = out_dir.resolve()
    policy_path = policy_path.resolve()

    cli_src = build_dir / "instantee"
    if not cli_src.is_file():
        raise RuntimeError(f"missing CLI binary: {cli_src}")

    system = platform.system().lower()
    arch = platform.machine().lower()
    if system != "darwin" or arch not in {"arm64", "aarch64"}:
        raise RuntimeError(
            f"macOS arm64 packaging required (got {system}/{arch})"
        )

    policy = json.loads(policy_path.read_text(encoding="utf-8"))
    package_name = f"instantee-macos-{version}-arm64"
    stage = out_dir / package_name
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    # Prefer cmake --install when the build tree is configured; fall back to
    # manual staging of the known payload for robustness.
    install_prefix = stage
    run(
        [
            "cmake",
            "--install",
            str(build_dir),
            "--prefix",
            str(install_prefix),
        ]
    )

    # Ensure authoring dist exists in the install tree (build before packaging).
    authoring_dist = (
        install_prefix
        / "share"
        / "instantee"
        / "authoring"
        / "packages"
        / "authoring"
        / "dist"
    )
    if not authoring_dist.is_dir():
        src_dist = repo_root / "authoring" / "packages" / "authoring" / "dist"
        if src_dist.is_dir():
            shutil.copytree(
                src_dist,
                authoring_dist,
                dirs_exist_ok=True,
            )

    smoke_dir = install_prefix / "share" / "instantee" / "smoke"
    smoke_dir.mkdir(parents=True, exist_ok=True)
    authoring = smoke_dir / "smoke.authoring.json"
    scene = smoke_dir / "smoke.scene.json"
    # Minimal authoring fixture from the repo (no personal paths).
    shutil.copy2(
        repo_root / "contracts" / "fixtures" / "authoring" / "positive" / "minimal.authoring.json",
        authoring,
    )
    style_bible = (
        install_prefix
        / "share"
        / "instantee"
        / "contracts"
        / "fixtures"
        / "style-bible"
        / "positive"
        / "editorial.style.json"
    )
    staged_cli = install_prefix / "bin" / "instantee"
    if not staged_cli.is_file():
        raise RuntimeError("cmake install did not place bin/instantee")
    run(
        [
            str(staged_cli),
            "compile",
            str(authoring),
            "--out",
            str(scene),
            "--style-bible",
            str(style_bible),
        ],
        cwd=install_prefix,
    )

    binary_info = probe_binary(staged_cli)
    if binary_info.get("non_system_libraries"):
        raise RuntimeError(
            "package CLI links non-system libraries: "
            + ", ".join(binary_info["non_system_libraries"])  # type: ignore[arg-type]
        )
    expected_rpath = policy["rpath"]["install_rpath"]
    rpaths = binary_info.get("rpaths") or []
    if expected_rpath not in rpaths:
        # cmake --install should rewrite rpath; if missing, fail closed.
        raise RuntimeError(
            f"INSTALL_RPATH {expected_rpath!r} not present on staged CLI "
            f"(found {rpaths!r})"
        )
    if binary_info.get("minos") not in {None, policy["platform"]["deployment_target"]}:
        # Allow match on declared deployment target.
        if str(binary_info.get("minos")) != str(policy["platform"]["deployment_target"]):
            raise RuntimeError(
                f"deployment target mismatch: binary minos={binary_info.get('minos')!r} "
                f"policy={policy['platform']['deployment_target']!r}"
            )

    signing = adhoc_sign(staged_cli)
    # Recompute hash after signing.
    binary_info["sha256"] = sha256_file(staged_cli)
    binary_info["signing"] = signing

    package_policy = {
        **policy,
        "package_name": package_name,
        "version": version,
        "certified_performance": False,
        "binary": binary_info,
    }
    (stage / "POLICY.json").write_text(
        json.dumps(package_policy, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    write_package_readme(stage / "README.md", version)
    manifest = build_manifest(stage)
    (stage / "MANIFEST.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    return stage


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / "build" / "release")
    parser.add_argument("--out-dir", type=Path, default=REPO_ROOT / "build" / "dist" / "macos")
    parser.add_argument("--policy", type=Path, default=DEFAULT_POLICY)
    parser.add_argument("--version", default="1.0.0")
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    args = parser.parse_args(argv)

    try:
        stage = package(
            repo_root=args.repo_root,
            build_dir=args.build_dir,
            out_dir=args.out_dir,
            policy_path=args.policy,
            version=args.version,
        )
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        print("macos packaging failed", file=sys.stderr)
        return 1

    print(f"macos package ok ({stage.name})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
