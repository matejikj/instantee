"""Tests for playbook dry-run coverage checks."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

import check_playbooks as playbooks


class PlaybookTests(unittest.TestCase):
    def test_repo_playbooks_pass(self) -> None:
        self.assertEqual(playbooks.run_checks(), [])

    def test_missing_case_section_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            book = root / "playbooks"
            book.mkdir()
            (book / "README.md").write_text("# playbooks\n", encoding="utf-8")
            (book / "troubleshooting.md").write_text(
                "## Case: export_failure\n\n### Symptoms\n\nx\n\n",
                encoding="utf-8",
            )
            (book / "benchmark.md").write_text(
                "certified sanitizer measure.py cold warm performance_claim never\n",
                encoding="utf-8",
            )
            with mock.patch.object(playbooks, "REPO_ROOT", root), mock.patch.object(
                playbooks, "PLAYBOOKS", book
            ), mock.patch.object(
                playbooks, "TROUBLESHOOTING", book / "troubleshooting.md"
            ), mock.patch.object(playbooks, "BENCHMARK", book / "benchmark.md"):
                errors = playbooks.run_checks()
            self.assertTrue(any("missing case" in error for error in errors))
            self.assertTrue(any("Reproduction" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
