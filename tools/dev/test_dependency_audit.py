"""Tests for dependency-manifest audit used by the release-gate CI."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import check_dependency_audit as audit


class DependencyAuditTests(unittest.TestCase):
    def test_clean_manifests_pass(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "vcpkg.json").write_text(
                json.dumps(
                    {
                        "name": "instantee",
                        "builtin-baseline": audit.EXPECTED_VCPKG_BASELINE,
                        "dependencies": [],
                    }
                ),
                encoding="utf-8",
            )
            (root / "package.json").write_text(
                json.dumps(
                    {
                        "devDependencies": {
                            name: "0.0.0" for name in sorted(audit.ALLOWED_ROOT_DEV_DEPENDENCIES)
                        }
                    }
                ),
                encoding="utf-8",
            )
            (root / "package-lock.json").write_text("{}", encoding="utf-8")
            self.assertEqual(audit.run_audit(root), [])

    def test_rejects_vcpkg_package_and_unexpected_npm_dep(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "vcpkg.json").write_text(
                json.dumps(
                    {
                        "builtin-baseline": "deadbeef",
                        "dependencies": ["fmt"],
                    }
                ),
                encoding="utf-8",
            )
            (root / "package.json").write_text(
                json.dumps(
                    {
                        "dependencies": {"left-pad": "1.0.0"},
                        "devDependencies": {"typescript": "5.9.3"},
                    }
                ),
                encoding="utf-8",
            )
            errors = audit.run_audit(root)
            joined = "\n".join(errors)
            self.assertIn("builtin-baseline drift", joined)
            self.assertIn("dependencies must stay empty", joined)
            self.assertIn("must not declare runtime dependencies", joined)
            self.assertIn("missing expected root package.json", joined)
            self.assertIn("missing package-lock.json", joined)


if __name__ == "__main__":
    unittest.main()
