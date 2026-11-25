#!/usr/bin/env python3
"""Validate the ownership resource inventory contract.

Ensures every exclusive runtime resource declares owner/borrow/completion/limit
and stop/error behaviour, and that cited evidence tests and headers exist.
Does not claim certified performance.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_INVENTORY = (
    REPO_ROOT / "contracts" / "ownership" / "resource-inventory-v1.json"
)


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def validate_inventory(
    *,
    repo_root: Path,
    inventory_path: Path,
) -> list[str]:
    errors: list[str] = []
    if not inventory_path.is_file():
        return [f"missing inventory: {inventory_path}"]

    inventory = load_json(inventory_path)
    if inventory.get("schema_version") != 1:
        errors.append("inventory schema_version must be 1")

    required = inventory.get("required_fields") or []
    expected_required = {
        "id",
        "module",
        "owner",
        "borrow",
        "completion",
        "limit",
        "stop_or_error",
        "move_only",
        "evidence_tests",
    }
    if set(required) != expected_required:
        errors.append(
            "required_fields must equal the normative ownership field set"
        )

    for doc in inventory.get("normative_docs") or []:
        if not (repo_root / doc).is_file():
            errors.append(f"missing normative doc: {doc}")

    resources = inventory.get("resources") or []
    if len(resources) < 8:
        errors.append("inventory must cover the core exclusive resource set")

    seen_ids: set[str] = set()
    for resource in resources:
        resource_id = resource.get("id")
        if not isinstance(resource_id, str) or not resource_id:
            errors.append("resource missing id")
            continue
        if resource_id in seen_ids:
            errors.append(f"duplicate resource id: {resource_id}")
        seen_ids.add(resource_id)

        for field in expected_required:
            if field not in resource:
                errors.append(f"{resource_id}: missing field {field}")
        if resource.get("move_only") is not True:
            errors.append(f"{resource_id}: exclusive resources must be move_only")

        for field in ("owner", "borrow", "completion", "limit", "stop_or_error"):
            value = resource.get(field)
            if not isinstance(value, str) or len(value.strip()) < 8:
                errors.append(f"{resource_id}: {field} must be a non-trivial string")

        header = resource.get("header")
        if isinstance(header, str) and header:
            if not (repo_root / header).is_file():
                errors.append(f"{resource_id}: missing header {header}")

        tests = resource.get("evidence_tests") or []
        if not isinstance(tests, list) or not tests:
            errors.append(f"{resource_id}: evidence_tests required")
            continue
        for test_path in tests:
            if not isinstance(test_path, str):
                errors.append(f"{resource_id}: invalid evidence test path")
                continue
            if not (repo_root / test_path).is_file():
                errors.append(f"{resource_id}: missing evidence test {test_path}")
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    parser.add_argument("--inventory", type=Path, default=DEFAULT_INVENTORY)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    errors = validate_inventory(
        repo_root=args.repo_root.resolve(),
        inventory_path=args.inventory.resolve(),
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
        print("ownership audit check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
    else:
        print("ownership audit check ok")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
