"""Tests for offline MSL PIN hash verification."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import check_shader_pin as pin


class ShaderPinTests(unittest.TestCase):
    def test_matching_hash_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            modules = root / "modules"
            modules.mkdir()
            metal = modules / "sample.metal"
            metal.write_text("// sample\n", encoding="utf-8")
            digest = pin.sha256_file(metal)
            pin_path = modules / "PIN.json"
            pin_path.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "modules": [
                            {
                                "id": "sample",
                                "source": "sample.metal",
                                "source_sha256": digest,
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            self.assertEqual(pin.verify_pin(pin_path), [])

    def test_drift_fails_and_write_repairs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            modules = root / "modules"
            modules.mkdir()
            metal = modules / "sample.metal"
            metal.write_text("// before\n", encoding="utf-8")
            pin_path = modules / "PIN.json"
            pin_path.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "modules": [
                            {
                                "id": "sample",
                                "source": "sample.metal",
                                "source_sha256": "0" * 64,
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            errors = pin.verify_pin(pin_path)
            self.assertTrue(any("source_sha256 drift" in error for error in errors))
            self.assertEqual(pin.verify_pin(pin_path, write=True), [])
            repaired = json.loads(pin_path.read_text(encoding="utf-8"))
            self.assertEqual(
                repaired["modules"][0]["source_sha256"], pin.sha256_file(metal)
            )


if __name__ == "__main__":
    unittest.main()
