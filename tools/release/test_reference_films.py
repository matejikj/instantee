"""Behavioral tests for publishable reference-film contracts."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import check_reference_films as check
import probe_mp4 as probe


REPO_ROOT = Path(__file__).resolve().parents[2]


class ReferenceFilmsTests(unittest.TestCase):
    def test_catalog_and_signoff_structure(self) -> None:
        errors = check.validate(
            repo_root=REPO_ROOT,
            catalog_path=REPO_ROOT
            / "contracts"
            / "release"
            / "reference-films"
            / "catalog-v1.json",
            signoff_path=REPO_ROOT
            / "contracts"
            / "release"
            / "reference-films"
            / "creative-signoff-v1.json",
            out_dir=None,
            require_files=False,
        )
        self.assertEqual(errors, [])

    def test_signoff_requires_phone_readability(self) -> None:
        signoff = json.loads(
            (
                REPO_ROOT
                / "contracts"
                / "release"
                / "reference-films"
                / "creative-signoff-v1.json"
            ).read_text(encoding="utf-8")
        )
        ids = {item["id"] for item in signoff["checklist_results"]}
        self.assertIn("phone_text_readability", ids)
        self.assertIn("style_bible_match", ids)
        self.assertEqual(signoff["status"], "approved")

    def test_validate_probe_rejects_audio(self) -> None:
        requirements = {
            "width": 1080,
            "height": 1920,
            "nominal_fps": 30,
            "audio_track_count": 0,
            "video_track_count": 1,
            "duration_sec": 60,
            "codec_name_allowlist": ["h264"],
        }
        bad = {
            "filename": "x.mp4",
            "width": 1080,
            "height": 1920,
            "nominal_fps": 30.0,
            "duration_sec": 60.0,
            "video_track_count": 1,
            "audio_track_count": 1,
            "codec_name": "h264",
        }
        errors = probe.validate_probe(bad, requirements)
        self.assertTrue(any("audio" in error for error in errors), errors)

    def test_require_files_needs_digest(self) -> None:
        catalog = json.loads(
            (
                REPO_ROOT
                / "contracts"
                / "release"
                / "reference-films"
                / "catalog-v1.json"
            ).read_text(encoding="utf-8")
        )
        for film in catalog["films"]:
            film["sha256"] = None
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            catalog_path = root / "catalog.json"
            catalog_path.write_text(
                json.dumps(catalog), encoding="utf-8"
            )
            errors = check.validate(
                repo_root=REPO_ROOT,
                catalog_path=catalog_path,
                signoff_path=REPO_ROOT
                / "contracts"
                / "release"
                / "reference-films"
                / "creative-signoff-v1.json",
                out_dir=root / "missing-mp4s",
                require_files=True,
            )
            self.assertTrue(
                any("sha256 required" in error for error in errors),
                errors,
            )

    def test_probe_parses_ffprobe_payload(self) -> None:
        payload = {
            "streams": [
                {
                    "codec_type": "video",
                    "codec_name": "h264",
                    "width": 1080,
                    "height": 1920,
                    "avg_frame_rate": "30/1",
                    "duration": "60.0",
                }
            ],
            "format": {"duration": "60.0"},
        }
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "sample.mp4"
            path.write_bytes(b"\x00\x00\x00\x18ftypmp42" + b"\x00" * 64)
            with mock.patch.object(probe, "_ffprobe", return_value=payload):
                result = probe.probe_mp4(path)
            self.assertEqual(result["width"], 1080)
            self.assertEqual(result["height"], 1920)
            self.assertEqual(result["audio_track_count"], 0)
            self.assertEqual(result["nominal_fps"], 30.0)


if __name__ == "__main__":
    unittest.main()
