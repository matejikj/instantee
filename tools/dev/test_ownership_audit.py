"""Behavioral tests for the ownership inventory checker."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import check_ownership_audit as check


REPO_ROOT = Path(__file__).resolve().parents[2]


class OwnershipAuditTests(unittest.TestCase):
    def test_repo_inventory_passes(self) -> None:
        errors = check.validate_inventory(
            repo_root=REPO_ROOT,
            inventory_path=REPO_ROOT
            / "contracts"
            / "ownership"
            / "resource-inventory-v1.json",
        )
        self.assertEqual(errors, [])

    def test_detects_missing_evidence_test(self) -> None:
        inventory = json.loads(
            (
                REPO_ROOT
                / "contracts"
                / "ownership"
                / "resource-inventory-v1.json"
            ).read_text(encoding="utf-8")
        )
        inventory["resources"][0]["evidence_tests"] = [
            "engine/tests/unit/runtime/does-not-exist.cpp"
        ]
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "inventory.json"
            path.write_text(json.dumps(inventory), encoding="utf-8")
            errors = check.validate_inventory(
                repo_root=REPO_ROOT, inventory_path=path
            )
            self.assertTrue(
                any("missing evidence test" in error for error in errors),
                errors,
            )

    def test_rejects_non_move_only(self) -> None:
        inventory = json.loads(
            (
                REPO_ROOT
                / "contracts"
                / "ownership"
                / "resource-inventory-v1.json"
            ).read_text(encoding="utf-8")
        )
        inventory["resources"][0]["move_only"] = False
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "inventory.json"
            path.write_text(json.dumps(inventory), encoding="utf-8")
            errors = check.validate_inventory(
                repo_root=REPO_ROOT, inventory_path=path
            )
            self.assertTrue(
                any("move_only" in error for error in errors),
                errors,
            )


if __name__ == "__main__":
    unittest.main()
