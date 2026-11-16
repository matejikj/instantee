"""Verify clang-tidy warnings-as-errors rejects a planted finding."""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class ClangTidyGateTests(unittest.TestCase):
    def test_planted_use_after_move_fails_warnings_as_errors(self) -> None:
        tidy = os.environ.get("CLANG_TIDY") or shutil.which("clang-tidy")
        if not tidy:
            self.skipTest("clang-tidy not available")

        with tempfile.TemporaryDirectory(prefix="instantee-tidy-") as temporary:
            source = Path(temporary) / "planted.cpp"
            source.write_text(
                "#include <memory>\n"
                "int main() {\n"
                "  auto pointer = std::make_unique<int>(1);\n"
                "  auto other = std::move(pointer);\n"
                "  return *pointer + *other;\n"
                "}\n",
                encoding="utf-8",
            )
            config = Path(temporary) / ".clang-tidy"
            config.write_text(
                "Checks: 'bugprone-use-after-move'\n"
                "WarningsAsErrors: '*'\n"
                "HeaderFilterRegex: '.*'\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    tidy,
                    str(source),
                    "--quiet",
                    "--warnings-as-errors=*",
                    f"--config-file={config}",
                    "--",
                    "-std=c++23",
                    "-stdlib=libc++",
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
                timeout=60,
            )
            combined = result.stdout + result.stderr
            self.assertNotEqual(result.returncode, 0, combined)
            self.assertIn("bugprone-use-after-move", combined)


if __name__ == "__main__":
    unittest.main()
