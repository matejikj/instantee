#!/usr/bin/env python3
"""Build an allowlisted measurement profile for the P3 geometry/text/mask series."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SYSTEM_STATE = ROOT / "tools/machine-profile/system_state.mm"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_revision() -> str:
    return subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()


def git_dirty() -> bool:
    status = subprocess.check_output(
        ["git", "status", "--porcelain"], cwd=ROOT, text=True)
    return bool(status.strip())


def git_diff_sha256() -> str:
    diff = subprocess.check_output(["git", "diff", "HEAD"], cwd=ROOT)
    return hashlib.sha256(diff).hexdigest()


def thermal_snapshot() -> dict:
    with tempfile.TemporaryDirectory(prefix="instantee-p3-profile-") as temporary:
        helper = Path(temporary) / "system-state"
        subprocess.run(
            ["/usr/bin/xcrun", "clang++", "-std=c++23", "-fobjc-arc",
             "-Wall", "-Wextra", "-Werror", "-framework", "Foundation",
             str(SYSTEM_STATE), "-o", str(helper)],
            check=True, capture_output=True, text=True, timeout=60)
        result = subprocess.run([str(helper)], check=True, capture_output=True,
                                text=True, timeout=10)
        state = json.loads(result.stdout)
    return {
        "thermal_state": state["thermal_state"],
        "physical_memory_bytes": state["physical_memory_bytes"],
        "logical_cpu_count": state["logical_cpu_count"],
    }


def dependency_lock_sha256() -> str:
    pins = [
        ROOT / "third_party/fonts/PIN.json",
        ROOT / "cmake/module_graph.json",
    ]
    digest = hashlib.sha256()
    for path in pins:
        digest.update(path.read_bytes())
    return digest.hexdigest()


def shaders_sha256() -> str:
    shader_root = ROOT / "engine/shaders"
    digest = hashlib.sha256()
    if shader_root.is_dir():
        for path in sorted(shader_root.rglob("*")):
            if path.is_file():
                digest.update(path.relative_to(ROOT).as_posix().encode())
                digest.update(path.read_bytes())
    return digest.hexdigest()


def compiler_flags_from_binary() -> list[str]:
    # Stable allowlisted flags for release measurement identity.
    return [
        "-DINSTANTEE_METRICS_VALIDATION=\"disabled\"",
        "-fobjc-arc",
        "-O2",
        "-g",
        "-DNDEBUG",
        "-std=c++23",
        "-arch=arm64",
        f"-mmacosx-version-min={platform.mac_ver()[0] or '14.0'}",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pedantic-errors",
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--gpu-cores", type=int, default=7)
    parser.add_argument("--model", default="MacBookAir10,1")
    parser.add_argument("--filesystem", default="apfs")
    args = parser.parse_args()

    binary = args.binary.resolve()
    if not binary.is_file():
        print("binary missing", file=sys.stderr)
        return 2

    thermal = thermal_snapshot()
    profile = {
        "machine_profile": {
            "model": args.model,
            "os_build": platform.mac_ver()[0] or "unknown",
            "logical_cpu_count": thermal["logical_cpu_count"],
            "gpu_cores": args.gpu_cores,
            "ram_bytes": thermal["physical_memory_bytes"],
            "filesystem": args.filesystem,
            "power_mode": "normal",
            "thermal_state_start": thermal["thermal_state"],
            "thermal_state_end": "unobserved_per_trial",
        },
        "build_profile": {
            "source_revision": git_revision(),
            "source_dirty": git_dirty(),
            "source_diff_sha256": git_diff_sha256(),
            "binary_sha256": sha256_file(binary),
            "toolchain_id": "pinned-native-arm64-cpp23",
            "compiler_flags": compiler_flags_from_binary(),
            "dependency_lock_sha256": dependency_lock_sha256(),
            "shaders_sha256": shaders_sha256(),
        },
        "workload": {
            "id": "P3-geometry-text-mask-60s-v1",
            "scene_sha256": shaders_sha256(),
            "asset_lock_sha256": dependency_lock_sha256(),
            "duration_frames": 1800,
            "width": 1080,
            "height": 1920,
            "frame_rate": {"numerator": 30, "denominator": 1},
            "seed_decimal": "0",
            "quality_tier": "p3-foundation-draft",
            "encoder_profile": "h264-high-6mbps-rec709-video-range-no-reorder",
        },
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(profile, indent=2) + "\n", encoding="utf-8")
    print(args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
