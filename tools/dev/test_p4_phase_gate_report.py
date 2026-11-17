"""Unit tests for the P4 Creative Direction phase-gate assembler."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import p4_phase_gate_report as gate

ROOT = Path(__file__).resolve().parents[2]


class P4PhaseGateReportTests(unittest.TestCase):
    def test_inventory_detects_incomplete(self):
        tracker = {
            "tasks": {
                "P4-001": {
                    "status": "done",
                    "acceptance": {"met": True},
                    "evidence": ["x"],
                },
                "P4-002": {
                    "status": "todo",
                    "acceptance": {"met": None},
                    "evidence": [],
                },
                "P4-020": {"status": "todo"},
            }
        }
        inv = gate.inventory(tracker)
        self.assertFalse(inv["all_prior_done_and_evidenced"])
        self.assertEqual(inv["missing_or_incomplete"], ["P4-002"])

    def test_hot_path_scan_clean_on_repo(self):
        result = gate.scan_hot_path_cds_includes(ROOT)
        self.assertTrue(result["cds_absent_from_hot_path"], msg=result["offenders"])
        self.assertGreater(result["scanned_files"], 0)

    def test_hot_path_scan_flags_direction_include(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "engine/src/runtime/src/bad.cpp"
            path.parent.mkdir(parents=True)
            path.write_text('#include <instantee/direction/style_bible.hpp>\n')
            result = gate.scan_hot_path_cds_includes(root)
            self.assertFalse(result["cds_absent_from_hot_path"])
            self.assertEqual(len(result["offenders"]), 1)

    def test_forbidden_edges_from_module_graph(self):
        graph = json.loads((ROOT / "cmake/module_graph.json").read_text())
        edges = gate.forbidden_direction_edges(graph)
        self.assertTrue(edges["clean"], msg=edges)

    def test_forbidden_edges_detect_leak(self):
        graph = {
            "modules": {
                "direction": {
                    "allowed_dependencies": ["diagnostics", "core-model", "typography"]
                },
                "runtime": {"allowed_dependencies": ["diagnostics", "direction"]},
                "render-core": {"allowed_dependencies": ["diagnostics"]},
            },
            "forbidden_examples": [],
        }
        edges = gate.forbidden_direction_edges(graph)
        self.assertFalse(edges["clean"])
        self.assertIn("runtime", edges["hot_modules_leaking_direction"])
        self.assertIn("typography", edges["direction_banned_deps_present"])

    def test_run_ctest_probe_parses_missing(self):
        with mock.patch("p4_phase_gate_report.subprocess.run") as run:
            run.return_value = mock.Mock(
                returncode=0, stdout="No tests were found!!!\n", stderr=""
            )
            result = gate.run_ctest_probe(Path("build/release"), "does_not_exist")
        self.assertFalse(result["found"])
        self.assertFalse(result["passed"])


if __name__ == "__main__":
    unittest.main()
