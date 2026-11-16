"""Negative and positive checks for the native module boundary rules."""

from __future__ import annotations

import shutil
import tempfile
import unittest
from pathlib import Path

import check_module_boundaries as boundaries

ROOT = Path(__file__).resolve().parents[2]


class ModuleBoundaryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.graph = boundaries.load_graph(ROOT / "cmake" / "module_graph.json")

    def test_repository_satisfies_graph(self) -> None:
        errors = boundaries.validate(self.graph, ROOT)
        self.assertEqual(errors, [])

    def test_forbidden_render_metal_include_in_core_model_fails(self) -> None:
        with tempfile.TemporaryDirectory(prefix="instantee-boundaries-") as temporary:
            sandbox = Path(temporary) / "repo"
            self._copy_minimal_tree(sandbox)
            victim = sandbox / "engine/src/core-model/src/time.cpp"
            text = victim.read_text(encoding="utf-8")
            victim.write_text(
                '#include <instantee/render_metal/device.hpp>\n' + text,
                encoding="utf-8",
            )
            errors = boundaries.validate(self.graph, sandbox)
            self.assertTrue(
                any("render-metal" in error and "core-model" in error for error in errors),
                errors,
            )

    def test_spike_include_in_production_target_fails(self) -> None:
        with tempfile.TemporaryDirectory(prefix="instantee-boundaries-") as temporary:
            sandbox = Path(temporary) / "repo"
            self._copy_minimal_tree(sandbox)
            victim = sandbox / "engine/src/export-core/src/write_all.cpp"
            text = victim.read_text(encoding="utf-8")
            victim.write_text(
                '#include <instantee/export_apple/workspace.hpp>\n' + text,
                encoding="utf-8",
            )
            errors = boundaries.validate(self.graph, sandbox)
            self.assertTrue(
                any("spike header" in error for error in errors),
                errors,
            )

    def test_missing_include_in_isolated_public_header_fails_to_compile(self) -> None:
        """A public header that uses a type without including it fails alone."""
        with tempfile.TemporaryDirectory(prefix="instantee-header-") as temporary:
            header = Path(temporary) / "broken.hpp"
            header.write_text(
                "#pragma once\n"
                "namespace instantee::core {\n"
                "struct UsesFrameRate { FrameRate rate; };\n"
                "}\n",
                encoding="utf-8",
            )
            tu = Path(temporary) / "tu.cpp"
            tu.write_text(f'#include "{header.name}"\n', encoding="utf-8")
            result = __import__("subprocess").run(
                [
                    "xcrun",
                    "clang++",
                    "-std=c++23",
                    "-stdlib=libc++",
                    "-fsyntax-only",
                    "-I",
                    str(temporary),
                    str(tu),
                ],
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertNotEqual(result.returncode, 0)
            combined = result.stdout + result.stderr
            self.assertTrue(
                "FrameRate" in combined or "unknown type" in combined or "does not name" in combined,
                combined,
            )

    def _copy_minimal_tree(self, sandbox: Path) -> None:
        for relative in (
            "cmake/module_graph.json",
            "engine/src/diagnostics",
            "engine/src/core-model",
            "engine/src/runtime",
            "engine/src/export-core",
            "engine/src/render-metal",
            "engine/native/export-apple",
            "engine/native/telemetry-apple",
            "engine/native/transfer-apple",
        ):
            source = ROOT / relative
            destination = sandbox / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            if source.is_dir():
                shutil.copytree(source, destination)
            else:
                shutil.copy2(source, destination)


if __name__ == "__main__":
    unittest.main()
