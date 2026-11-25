"""Tests for macOS packaging helpers."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import smoke_install_macos as smoke


class PackageMacosTests(unittest.TestCase):
    def test_policy_file_exists(self) -> None:
        root = Path(__file__).resolve().parents[2]
        self.assertTrue((root / "packaging" / "macos" / "policy-v1.json").is_file())
        self.assertTrue((root / "packaging" / "macos" / "README.md").is_file())
        self.assertTrue((root / "cmake" / "InstallRelease.cmake").is_file())

    def test_smoke_rejects_cmake_invocation(self) -> None:
        with self.assertRaises(RuntimeError):
            smoke.run(["cmake", "--version"])

    def test_looks_like_mp4(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "a.mp4"
            path.write_bytes(b"\x00\x00\x00\x18ftypmp42")
            self.assertTrue(smoke.looks_like_mp4(path))


if __name__ == "__main__":
    unittest.main()
