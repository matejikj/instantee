"""Tests for quality lint and accessibility release policy checks."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import check_quality_release as quality


def _write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload), encoding="utf-8")


def _clean_policy(root: Path) -> dict:
    findings = {
        "schema_version": "lint-findings/1",
        "findings": [],
    }
    _write_json(root / "examples" / "film" / "findings.json", findings)
    _write_json(
        root / "review" / "review-record.json",
        {
            "status": "approved",
            "checklist_results": [
                {"id": "safe_area", "passed": True},
                {"id": "phone_legibility", "passed": True},
            ],
        },
    )
    _write_json(
        root / "styles" / "editorial.style.json",
        {
            "tokens": {
                "safe_area": {
                    "mode": "reels_overlay",
                    "top_px": 100,
                    "bottom_px": 200,
                    "left_px": 24,
                    "right_px": 24,
                }
            }
        },
    )
    (root / "example.mjs").write_text("// stub\n", encoding="utf-8")
    return {
        "schema_version": 1,
        "enforcement": "strict",
        "blocking_codes": ["hard_overlap", "important_outside_safe_area"],
        "warning_codes": ["text_overflow"],
        "released_examples": [
            {
                "id": "film",
                "kind": "style-film",
                "findings": "examples/film/findings.json",
                "waivers": None,
                "legibility": {
                    "kind": "visual-review",
                    "review_record": "review/review-record.json",
                    "required_checklist_ids": ["safe_area", "phone_legibility"],
                },
            },
            {
                "id": "authoring",
                "kind": "authoring-example",
                "example_entry": "example.mjs",
                "findings": "examples/film/findings.json",
                "waivers": None,
                "legibility": {
                    "kind": "style-bible-safe-area",
                    "style_packages": ["styles/editorial.style.json"],
                },
            },
        ],
    }


class QualityReleaseTests(unittest.TestCase):
    def test_clean_policy_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            policy = _clean_policy(root)
            policy_path = root / "policy.json"
            _write_json(policy_path, policy)
            report = quality.run_policy(root, policy_path)
            self.assertTrue(report["ok"], report["errors"])
            self.assertEqual(len(report["examples"]), 2)

    def test_unapproved_error_blocks_release(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            policy = _clean_policy(root)
            _write_json(
                root / "examples" / "film" / "findings.json",
                {
                    "schema_version": "lint-findings/1",
                    "findings": [
                        {
                            "code": "hard_overlap",
                            "severity": "error",
                            "path": "/shots/0/nodes/title",
                            "element_id": "title",
                            "message": "overlap",
                        }
                    ],
                },
            )
            policy_path = root / "policy.json"
            _write_json(policy_path, policy)
            report = quality.run_policy(root, policy_path)
            self.assertFalse(report["ok"])
            self.assertTrue(
                any("unapproved error findings" in err for err in report["errors"])
            )

    def test_approved_waiver_clears_blocking_error(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            policy = _clean_policy(root)
            _write_json(
                root / "examples" / "film" / "findings.json",
                {
                    "schema_version": "lint-findings/1",
                    "findings": [
                        {
                            "code": "hard_overlap",
                            "severity": "error",
                            "path": "/shots/0/nodes/title",
                            "element_id": "title",
                            "message": "overlap",
                        }
                    ],
                },
            )
            _write_json(
                root / "examples" / "film" / "waivers.json",
                {
                    "schema_version": "lint-waivers/1",
                    "waivers": [
                        {
                            "code": "hard_overlap",
                            "path": "/shots/0/nodes/title",
                            "element_id": "title",
                            "reason": "Intentional stacked opener",
                            "approval_id": "waiver-test-001",
                        }
                    ],
                },
            )
            policy["released_examples"][0]["waivers"] = "examples/film/waivers.json"
            policy["released_examples"][1]["findings"] = "examples/film/empty.json"
            _write_json(
                root / "examples" / "film" / "empty.json",
                {"schema_version": "lint-findings/1", "findings": []},
            )
            policy_path = root / "policy.json"
            _write_json(policy_path, policy)
            report = quality.run_policy(root, policy_path)
            self.assertTrue(report["ok"], report["errors"])

    def test_failed_phone_legibility_blocks(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            policy = _clean_policy(root)
            _write_json(
                root / "review" / "review-record.json",
                {
                    "status": "approved",
                    "checklist_results": [
                        {"id": "safe_area", "passed": True},
                        {"id": "phone_legibility", "passed": False},
                    ],
                },
            )
            policy_path = root / "policy.json"
            _write_json(policy_path, policy)
            report = quality.run_policy(root, policy_path)
            self.assertFalse(report["ok"])
            self.assertTrue(
                any("phone_legibility" in err for err in report["errors"])
            )

    def test_zero_safe_area_blocks_authoring_example(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            policy = _clean_policy(root)
            _write_json(
                root / "styles" / "editorial.style.json",
                {
                    "tokens": {
                        "safe_area": {
                            "mode": "reels_overlay",
                            "top_px": 0,
                            "bottom_px": 0,
                            "left_px": 0,
                            "right_px": 0,
                        }
                    }
                },
            )
            policy_path = root / "policy.json"
            _write_json(policy_path, policy)
            report = quality.run_policy(root, policy_path)
            self.assertFalse(report["ok"])
            self.assertTrue(any("must not all be zero" in err for err in report["errors"]))

    def test_repo_policy_passes(self) -> None:
        report = quality.run_policy(quality.REPO_ROOT, quality.DEFAULT_POLICY)
        self.assertTrue(report["ok"], report["errors"])
        self.assertGreaterEqual(len(report["examples"]), 4)


if __name__ == "__main__":
    unittest.main()
