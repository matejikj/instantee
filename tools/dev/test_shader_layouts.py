"""Tests for CPU/GPU shader layout contract verification."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import check_shader_layouts as layouts


class ShaderLayoutTests(unittest.TestCase):
    def test_matching_contract_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            metal = root / "a.metal"
            metal.write_text("struct CanvasUniforms { float2 canvas; };\n", encoding="utf-8")
            cpp = root / "a.cpp"
            cpp.write_text(
                "struct CanvasUniforms { float canvas[2]; float pad[2]; };\n"
                "static_assert(sizeof(CanvasUniforms) == 16, \"must match MSL layout\");\n",
                encoding="utf-8",
            )
            contract = root / "layouts.json"
            contract.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "layouts": [
                            {
                                "id": "demo.CanvasUniforms",
                                "metal_source": "a.metal",
                                "metal_struct": "CanvasUniforms",
                                "cpu_sources": ["a.cpp"],
                                "cpu_type": "CanvasUniforms",
                                "size_bytes": 16,
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            self.assertEqual(layouts.verify_layouts(contract, root), [])

    def test_missing_sizeof_assert_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "a.metal").write_text("struct CanvasUniforms {};\n", encoding="utf-8")
            (root / "a.cpp").write_text("struct CanvasUniforms {};\n", encoding="utf-8")
            contract = root / "layouts.json"
            contract.write_text(
                json.dumps(
                    {
                        "layouts": [
                            {
                                "id": "demo.CanvasUniforms",
                                "metal_source": "a.metal",
                                "metal_struct": "CanvasUniforms",
                                "cpu_sources": ["a.cpp"],
                                "cpu_type": "CanvasUniforms",
                                "size_bytes": 16,
                            }
                        ]
                    }
                ),
                encoding="utf-8",
            )
            errors = layouts.verify_layouts(contract, root)
            self.assertTrue(any("missing static_assert" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
