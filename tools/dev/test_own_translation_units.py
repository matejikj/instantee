"""Tests for own translation-unit selection used by clang-tidy CI."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import own_translation_units as otu


class OwnTranslationUnitsTests(unittest.TestCase):
    def test_excludes_vendored_paths(self) -> None:
        self.assertTrue(otu.is_excluded("engine/src/asset-pipeline/third_party/stb/x.cpp"))
        self.assertTrue(otu.is_excluded("engine/native/foo/metal-cpp/Foundation.hpp"))
        self.assertFalse(otu.is_excluded("engine/src/runtime/src/frame_queue.cpp"))

    def test_intersects_compile_commands(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tracked = [
                "engine/src/runtime/src/a.cpp",
                "engine/src/runtime/src/b.cpp",
                "engine/src/asset-pipeline/third_party/stb/x.cpp",
                "engine/src/runtime/include/instantee/runtime/a.hpp",
            ]
            compile_commands = root / "compile_commands.json"
            compile_commands.write_text(
                json.dumps(
                    [
                        {
                            "directory": str(root),
                            "file": str(root / "engine/src/runtime/src/a.cpp"),
                            "command": "clang++ -c a.cpp",
                        }
                    ]
                ),
                encoding="utf-8",
            )
            with mock.patch.object(otu, "_git_ls_files", return_value=tracked):
                selected = otu.list_own_translation_units(
                    repo_root=root,
                    compile_commands=compile_commands,
                )
            self.assertEqual(selected, ["engine/src/runtime/src/a.cpp"])


if __name__ == "__main__":
    unittest.main()
