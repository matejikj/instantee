#!/usr/bin/env python3
"""Verify offline MSL module PIN hashes match tracked shader sources."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PIN = REPO_ROOT / "engine" / "shaders" / "modules" / "PIN.json"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def verify_pin(pin_path: Path, *, write: bool = False) -> list[str]:
    errors: list[str] = []
    document = json.loads(pin_path.read_text(encoding="utf-8"))
    modules = document.get("modules")
    if not isinstance(modules, list) or not modules:
        return ["PIN.json modules list is empty or missing"]

    pin_dir = pin_path.parent
    changed = False
    for module in modules:
        module_id = module.get("id", "<unknown>")
        relative = module.get("source")
        expected = module.get("source_sha256")
        if not relative or not expected:
            errors.append(f"{module_id}: missing source or source_sha256")
            continue
        source = (pin_dir / relative).resolve()
        if not source.is_file():
            errors.append(f"{module_id}: missing source {relative}")
            continue
        actual = sha256_file(source)
        if actual == expected:
            continue
        if write:
            module["source_sha256"] = actual
            changed = True
            print(f"updated {module_id}: {actual}")
        else:
            errors.append(
                f"{module_id}: source_sha256 drift for {relative} "
                f"(pin={expected}, actual={actual})"
            )

    if write and changed:
        pin_path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    return errors


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--pin",
        type=Path,
        default=DEFAULT_PIN,
        help="path to engine/shaders/modules/PIN.json",
    )
    parser.add_argument(
        "--write",
        action="store_true",
        help="rewrite source_sha256 fields from the current .metal sources",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if not args.pin.is_file():
        print(f"missing PIN file: {args.pin}", file=sys.stderr)
        return 1
    errors = verify_pin(args.pin, write=args.write)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    try:
        display = args.pin.resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:
        display = args.pin.as_posix()
    print(f"shader PIN ok ({display})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
