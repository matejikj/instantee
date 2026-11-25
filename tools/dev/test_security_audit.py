"""Tests for offline security and license audit."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import check_security_audit as security


def _write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload), encoding="utf-8")


class SecurityAuditTests(unittest.TestCase):
    def test_repo_audit_passes(self) -> None:
        result = security.run_audit()
        report = result["report"]
        self.assertTrue(report["ok"], report.get("unresolved_critical"))
        self.assertEqual(report["summary"]["critical"], 0)
        self.assertFalse(report["certified_performance"])
        self.assertGreaterEqual(report["summary"]["third_party_pins"], 9)
        self.assertGreaterEqual(report["summary"]["release_asset_entries"], 1)
        self.assertIn("bomFormat", result["sbom"])
        self.assertEqual(result["sbom"]["bomFormat"], "CycloneDX")

    def test_disallowed_license_is_critical(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            allowed = root / "allowed.json"
            _write_json(allowed, {"schema_version": 1, "licenses": ["MIT"]})
            findings: list[dict] = []
            with mock.patch.object(security, "REPO_ROOT", root):
                security.finding(
                    findings,
                    severity="critical",
                    code="license_not_allowed",
                    path="demo",
                    message="license not allowed: 'GPL-3.0'",
                )
            self.assertEqual(findings[0]["severity"], "critical")

    def test_font_hash_mismatch_detected(self) -> None:
        findings: list[dict] = []
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fonts_dir = root / "third_party" / "fonts"
            fonts_dir.mkdir(parents=True)
            (fonts_dir / "NotoSans-Regular.ttf").write_bytes(b"not-a-font")
            (fonts_dir / "OFL-Noto-Sans.txt").write_text("OFL\n", encoding="utf-8")
            _write_json(
                fonts_dir / "PIN.json",
                {
                    "schema_version": 1,
                    "name": "pinned-fallback-fonts",
                    "fonts": [
                        {
                            "file": "NotoSans-Regular.ttf",
                            "family": "Noto Sans",
                            "license": "OFL-1.1",
                            "license_file": "OFL-Noto-Sans.txt",
                            "archive_sha256": "0" * 64,
                        }
                    ],
                },
            )
            allowed = {"OFL-1.1"}
            with mock.patch.object(security, "THIRD_PARTY", root / "third_party"):
                with mock.patch.object(security, "REPO_ROOT", root):
                    security.audit_third_party_pins(findings, allowed)
            codes = {item["code"] for item in findings if item["severity"] == "critical"}
            self.assertIn("font_hash_mismatch", codes)

    def test_metal_cpp_mismatch_is_critical(self) -> None:
        findings: list[dict] = []
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            metal = root / "third_party" / "metal-cpp"
            metal.mkdir(parents=True)
            (metal / "LICENSE.txt").write_text("Apache\n", encoding="utf-8")
            (metal / "Metal").mkdir()
            (metal / "Metal" / "Metal.hpp").write_text("//\n", encoding="utf-8")
            _write_json(
                metal / "PIN.json",
                {
                    "schema_version": 1,
                    "name": "metal-cpp",
                    "license": "Apache-2.0",
                    "tag": "wrong-tag",
                    "commit": "deadbeef",
                    "archive_sha256": "0" * 64,
                },
            )
            with mock.patch.object(security, "THIRD_PARTY", root / "third_party"):
                result = security.audit_metal_cpp(findings)
            self.assertFalse(result["ok"])
            self.assertTrue(
                any(item["code"] == "metal_cpp_pin_mismatch" for item in findings)
            )


if __name__ == "__main__":
    unittest.main()
