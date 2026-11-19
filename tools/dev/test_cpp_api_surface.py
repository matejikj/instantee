"""Behavioral checks for the C++ public API surface linter."""

from __future__ import annotations

import json
import shutil
import tempfile
import unittest
from pathlib import Path

import check_cpp_api_surface as api

ROOT = Path(__file__).resolve().parents[2]


class CppApiSurfaceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.graph = api.load_graph(ROOT / "cmake" / "module_graph.json")
        self.snapshot = ROOT / "contracts" / "cpp" / "public-headers-v1.snapshot.json"

    def test_repository_matches_frozen_surface(self) -> None:
        errors = api.validate(self.graph, ROOT, self.snapshot)
        self.assertEqual(errors, [])

    def test_forbidden_metal_include_in_core_public_header_fails(self) -> None:
        with tempfile.TemporaryDirectory(prefix="instantee-api-") as temporary:
            sandbox = Path(temporary) / "repo"
            self._copy_minimal_tree(sandbox)
            victim = (
                sandbox
                / "engine/src/core-model/include/instantee/core_model/time.hpp"
            )
            text = victim.read_text(encoding="utf-8")
            victim.write_text('#include <Metal/Metal.hpp>\n' + text, encoding="utf-8")
            errors = api.validate_headers(self.graph, sandbox)
            self.assertTrue(
                any("forbidden public include" in error and "Metal/" in error for error in errors),
                errors,
            )

    def test_apple_token_in_domain_public_header_fails(self) -> None:
        with tempfile.TemporaryDirectory(prefix="instantee-api-") as temporary:
            sandbox = Path(temporary) / "repo"
            self._copy_minimal_tree(sandbox)
            victim = (
                sandbox
                / "engine/src/core-model/include/instantee/core_model/time.hpp"
            )
            text = victim.read_text(encoding="utf-8")
            victim.write_text(
                text + "\nnamespace leak { using Handle = CVPixelBufferRef; }\n",
                encoding="utf-8",
            )
            errors = api.validate_headers(self.graph, sandbox)
            self.assertTrue(
                any("platform token" in error for error in errors),
                errors,
            )

    def test_unintended_impl_public_header_fails(self) -> None:
        with tempfile.TemporaryDirectory(prefix="instantee-api-") as temporary:
            sandbox = Path(temporary) / "repo"
            self._copy_minimal_tree(sandbox)
            planted = (
                sandbox
                / "engine/src/core-model/include/instantee/core_model/time_impl.hpp"
            )
            planted.write_text("#pragma once\nstruct Hidden {};\n", encoding="utf-8")
            errors = api.validate_headers(self.graph, sandbox)
            self.assertTrue(
                any("unintended public header" in error for error in errors),
                errors,
            )

    def test_public_apple_framework_link_fails(self) -> None:
        with tempfile.TemporaryDirectory(prefix="instantee-api-") as temporary:
            sandbox = Path(temporary) / "repo"
            self._copy_minimal_tree(sandbox)
            cmake = sandbox / "engine/src/render-metal/CMakeLists.txt"
            text = cmake.read_text(encoding="utf-8")
            poisoned = text.replace(
                "PRIVATE instantee_build_options\n"
                '    "${INSTANTEE_METAL_FRAMEWORK}"\n'
                '    "${INSTANTEE_FOUNDATION_FRAMEWORK}")',
                "PRIVATE instantee_build_options\n"
                "  PUBLIC\n"
                '    "${INSTANTEE_METAL_FRAMEWORK}"\n'
                '    "${INSTANTEE_FOUNDATION_FRAMEWORK}")',
            )
            self.assertNotEqual(text, poisoned)
            cmake.write_text(poisoned, encoding="utf-8")
            errors = api.validate_cmake(self.graph, sandbox)
            self.assertTrue(
                any("links Apple framework" in error and "PUBLIC" in error for error in errors),
                errors,
            )

    def test_new_public_header_without_snapshot_update_fails(self) -> None:
        with tempfile.TemporaryDirectory(prefix="instantee-api-") as temporary:
            sandbox = Path(temporary) / "repo"
            self._copy_minimal_tree(sandbox)
            snapshot = sandbox / "contracts/cpp/public-headers-v1.snapshot.json"
            snapshot.parent.mkdir(parents=True, exist_ok=True)
            # Freeze the current inventory, then add a header.
            snap = api.collect_snapshot(sandbox)
            snapshot.write_text(json.dumps(snap, indent=2) + "\n", encoding="utf-8")
            planted = (
                sandbox
                / "engine/src/core-model/include/instantee/core_model/extra_public.hpp"
            )
            planted.write_text("#pragma once\nstruct Extra {};\n", encoding="utf-8")
            errors = api.validate_snapshot(sandbox, snapshot)
            self.assertTrue(
                any("unintended public header" in error for error in errors),
                errors,
            )

    def _copy_minimal_tree(self, sandbox: Path) -> None:
        for relative in (
            "cmake/module_graph.json",
            "engine/src/core-model",
            "engine/src/diagnostics",
            "engine/src/render-core",
            "engine/src/render-metal",
            "engine/native/export-apple",
            "engine/native/transfer-apple",
            "engine/native/telemetry-apple",
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
