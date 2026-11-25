"""Tests for tutorial end-to-end docs runner helpers."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

import run_tutorial_e2e as tutorials


class TutorialE2ETests(unittest.TestCase):
    def test_tutorial_scripts_exist(self) -> None:
        for item in tutorials.TUTORIALS:
            self.assertTrue(
                (tutorials.REPO_ROOT / item["script"]).is_file(), item["script"]
            )
            self.assertTrue(
                (tutorials.REPO_ROOT / item["style_bible"]).is_file(),
                item["style_bible"],
            )

    def test_looks_like_mp4(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "clip.mp4"
            path.write_bytes(b"\x00\x00\x00\x18ftypmp42\x00\x00\x00\x00")
            self.assertTrue(tutorials.looks_like_mp4(path))
            path.write_bytes(b"not-a-video")
            self.assertFalse(tutorials.looks_like_mp4(path))

    def test_missing_cli_fails_main(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary) / "work"
            code = tutorials.main(
                [
                    "--cli",
                    str(Path(temporary) / "missing-binary"),
                    "--work-dir",
                    str(work),
                ]
            )
            self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
