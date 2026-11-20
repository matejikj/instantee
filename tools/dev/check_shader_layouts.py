#!/usr/bin/env python3
"""Fail when CPU/GPU shader layout contracts drift from sources."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CONTRACT = REPO_ROOT / "contracts" / "shaders" / "cpu-gpu-layouts-v1.json"


def _struct_present(source: str, name: str) -> bool:
    return re.search(rf"\bstruct\s+{re.escape(name)}\b", source) is not None


def _sizeof_assert_present(source: str, type_name: str, size_bytes: int) -> bool:
    pattern = (
        rf"static_assert\s*\(\s*sizeof\s*\(\s*{re.escape(type_name)}\s*\)\s*"
        rf"==\s*{size_bytes}\b"
    )
    return re.search(pattern, source) is not None


def verify_layouts(contract_path: Path, repo_root: Path = REPO_ROOT) -> list[str]:
    document = json.loads(contract_path.read_text(encoding="utf-8"))
    layouts = document.get("layouts")
    if not isinstance(layouts, list) or not layouts:
        return ["layout contract has no layouts"]

    errors: list[str] = []
    for layout in layouts:
        layout_id = layout.get("id", "<unknown>")
        metal_rel = layout.get("metal_source")
        metal_struct = layout.get("metal_struct")
        cpu_type = layout.get("cpu_type")
        size_bytes = layout.get("size_bytes")
        cpu_sources = layout.get("cpu_sources") or []
        if not all([metal_rel, metal_struct, cpu_type, isinstance(size_bytes, int)]):
            errors.append(f"{layout_id}: incomplete layout entry")
            continue

        metal_path = repo_root / metal_rel
        if not metal_path.is_file():
            errors.append(f"{layout_id}: missing metal source {metal_rel}")
        else:
            metal_text = metal_path.read_text(encoding="utf-8")
            if not _struct_present(metal_text, metal_struct):
                errors.append(
                    f"{layout_id}: metal struct {metal_struct} missing in {metal_rel}"
                )

        if not cpu_sources:
            errors.append(f"{layout_id}: cpu_sources is empty")
        for cpu_rel in cpu_sources:
            cpu_path = repo_root / cpu_rel
            if not cpu_path.is_file():
                errors.append(f"{layout_id}: missing cpu source {cpu_rel}")
                continue
            cpu_text = cpu_path.read_text(encoding="utf-8")
            if not _struct_present(cpu_text, cpu_type):
                errors.append(
                    f"{layout_id}: cpu type {cpu_type} missing in {cpu_rel}"
                )
            if not _sizeof_assert_present(cpu_text, cpu_type, size_bytes):
                errors.append(
                    f"{layout_id}: missing static_assert(sizeof({cpu_type}) == "
                    f"{size_bytes}) in {cpu_rel}"
                )
    return errors


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--contract",
        type=Path,
        default=DEFAULT_CONTRACT,
        help="CPU/GPU layout contract JSON",
    )
    parser.add_argument(
        "--repo",
        type=Path,
        default=REPO_ROOT,
        help="repository root",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if not args.contract.is_file():
        print(f"missing layout contract: {args.contract}", file=sys.stderr)
        return 1
    errors = verify_layouts(args.contract, args.repo)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    try:
        display = args.contract.resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:
        display = args.contract.as_posix()
    print(f"shader layout contract ok ({display})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
