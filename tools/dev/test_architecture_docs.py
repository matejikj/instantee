"""Tests for architecture documentation coverage checks."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

import check_architecture_docs as arch


class ArchitectureDocsTests(unittest.TestCase):
    def test_repo_docs_pass(self) -> None:
        errors = arch.run_checks()
        self.assertEqual(errors, [])

    def test_missing_module_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            docs = root / "architecture"
            docs.mkdir(parents=True)
            for name in arch.REQUIRED_DOCS:
                (docs / name).write_text("# stub\n", encoding="utf-8")
            (root / "ARCHITECTURE.md").write_text(
                "architecture/modules.md\n"
                "architecture/extending-styles.md\n"
                "architecture/adr-index.md\n",
                encoding="utf-8",
            )
            graph = {
                "modules": {
                    "diagnostics": {
                        "include_prefix": "instantee/diagnostics/",
                        "role": "production",
                    }
                }
            }
            with mock.patch.object(arch, "REPO_ROOT", root), mock.patch.object(
                arch, "DOCS", docs
            ):
                errors = arch.check_module_catalog(graph)
            self.assertTrue(any("diagnostics" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
