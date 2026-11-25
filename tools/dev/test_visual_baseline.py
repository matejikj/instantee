"""Tests for cross-style visual baseline approval checks."""

from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import check_visual_baseline as baseline


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _write_style(root: Path, style_id: str, *, approve: bool = True) -> dict:
    goldens = root / f"goldens-{style_id}"
    review = root / f"review-{style_id}"
    goldens.mkdir()
    review.mkdir()
    png = goldens / "frame.png"
    structural = goldens / "frame.structural.txt"
    png.write_bytes(b"\x89PNG\r\n\x1a\n" + style_id.encode())
    structural.write_text(f"structural-{style_id}\n", encoding="utf-8")
    png_digest = _sha(png.read_bytes())
    structural_digest = _sha(structural.read_bytes())
    workload = f"W-{style_id}"
    manifest = {
        "schema_version": "1.0.0",
        "workload": workload,
        "artifacts": [
            {"id": "frame.structural.txt", "kind": "structural", "sha256": structural_digest},
            {"id": "frame.png", "kind": "png", "sha256": png_digest},
        ],
    }
    manifest_path = goldens / "manifest.json"
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
    (goldens / "CHANGE_RECORD.json").write_text(
        json.dumps(
            {
                "schema_version": "1.0.0",
                "entries": [
                    {
                        "artifact_id": "frame.structural.txt",
                        "previous_sha256": "",
                        "next_sha256": structural_digest,
                        "reason": "bootstrap",
                        "approved_by_role": "reviewer",
                        "approved_at_utc": "2026-01-01T00:00:00Z",
                    },
                    {
                        "artifact_id": "frame.png",
                        "previous_sha256": "",
                        "next_sha256": png_digest,
                        "reason": "bootstrap",
                        "approved_by_role": "reviewer",
                        "approved_at_utc": "2026-01-01T00:00:00Z",
                    },
                ],
            }
        ),
        encoding="utf-8",
    )
    (review / "tolerances.json").write_text(
        json.dumps({"max_channel_delta": 0, "max_mismatch_ratio": 0.0}),
        encoding="utf-8",
    )
    (review / "review-record.json").write_text(
        json.dumps(
            {
                "status": "approved" if approve else "rejected",
                "golden_manifest_sha256": _sha(manifest_path.read_bytes()),
                "tolerances": {"max_channel_delta": 0, "max_mismatch_ratio": 0.0},
                "checklist_results": [
                    {"id": "phone_legibility", "passed": True},
                    {"id": "reference_frames", "passed": True},
                ],
            }
        ),
        encoding="utf-8",
    )
    return {
        "id": style_id,
        "workload": workload,
        "goldens_dir": goldens.name,
        "review_dir": review.name,
        "ctest_name": f"test_{style_id}",
        "readability_checklist_ids": ["phone_legibility", "reference_frames"],
    }


class VisualBaselineTests(unittest.TestCase):
    def test_clean_registry_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            styles = [
                _write_style(root, "editorial"),
                _write_style(root, "notebook"),
                _write_style(root, "paper"),
            ]
            registry = root / "registry.json"
            registry.write_text(
                json.dumps(
                    {
                        "baseline_platform": {
                            "os_family": "macos",
                            "arch": "arm64",
                            "gpu_family": "apple-silicon",
                        },
                        "default_tolerances": {
                            "max_channel_delta": 0,
                            "max_mismatch_ratio": 0.0,
                        },
                        "styles": styles,
                    }
                ),
                encoding="utf-8",
            )
            with mock.patch.object(
                baseline,
                "detect_platform",
                return_value={
                    "os_family": "macos",
                    "arch": "arm64",
                    "gpu_family": "apple-silicon",
                },
            ):
                self.assertEqual(
                    baseline.verify_registry(
                        registry, repo_root=root, enforce_platform=True
                    ),
                    [],
                )

    def test_unreviewed_pixel_change_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            style = _write_style(root, "editorial")
            png = root / style["goldens_dir"] / "frame.png"
            png.write_bytes(b"\x89PNG\r\n\x1a\nCHANGED")
            registry = root / "registry.json"
            registry.write_text(
                json.dumps(
                    {
                        "default_tolerances": {
                            "max_channel_delta": 0,
                            "max_mismatch_ratio": 0.0,
                        },
                        "styles": [style, _write_style(root, "notebook"), _write_style(root, "paper")],
                    }
                ),
                encoding="utf-8",
            )
            errors = baseline.verify_registry(registry, repo_root=root)
            self.assertTrue(
                any("unreviewed or stale digest" in error for error in errors),
                errors,
            )

    def test_failed_readability_checklist_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            style = _write_style(root, "editorial")
            review_path = root / style["review_dir"] / "review-record.json"
            review = json.loads(review_path.read_text(encoding="utf-8"))
            review["checklist_results"] = [
                {"id": "phone_legibility", "passed": False},
                {"id": "reference_frames", "passed": True},
            ]
            review_path.write_text(json.dumps(review), encoding="utf-8")
            registry = root / "registry.json"
            registry.write_text(
                json.dumps(
                    {
                        "default_tolerances": {
                            "max_channel_delta": 0,
                            "max_mismatch_ratio": 0.0,
                        },
                        "styles": [style, _write_style(root, "notebook"), _write_style(root, "paper")],
                    }
                ),
                encoding="utf-8",
            )
            errors = baseline.verify_registry(registry, repo_root=root)
            self.assertTrue(
                any("readability checklist failed: phone_legibility" in error for error in errors),
                errors,
            )


if __name__ == "__main__":
    unittest.main()
