"""Unit tests for the P5 Editorial phase-gate assembler."""

from __future__ import annotations

import unittest
from pathlib import Path
from unittest import mock

import p5_phase_gate_report as gate

ROOT = Path(__file__).resolve().parents[2]


class P5PhaseGateReportTests(unittest.TestCase):
    def test_inventory_detects_incomplete(self):
        tracker = {
            "tasks": {
                "P5-001": {
                    "status": "done",
                    "acceptance": {"met": True},
                    "evidence": ["x"],
                },
                "P5-002": {
                    "status": "todo",
                    "acceptance": {"met": None},
                    "evidence": [],
                },
                "P5-020": {"status": "todo"},
            }
        }
        inv = gate.inventory(tracker)
        self.assertFalse(inv["all_prior_done_and_evidenced"])
        self.assertEqual(inv["missing_or_incomplete"], ["P5-002"])

    def test_inventory_passes_complete_tracker_slice(self):
        tracker = {
            "tasks": {
                f"P5-{i:03d}": {
                    "status": "done",
                    "acceptance": {"met": True},
                    "evidence": [f"e{i}"],
                }
                for i in range(1, 20)
            }
        }
        tracker["tasks"]["P5-020"] = {"status": "todo"}
        inv = gate.inventory(tracker)
        self.assertTrue(inv["all_prior_done_and_evidenced"])
        self.assertEqual(inv["phase_task_count_excluding_gate"], 19)

    def test_summarize_series_requires_wall_and_gate(self):
        baseline = {
            "series_pass": True,
            "decision": {"threshold_gate": True},
            "thresholds": {"export_wall_ms_max": 360000},
            "summary": {"cold_median_export_wall_ms": 105000.0},
        }
        summary = gate.summarize_series(baseline)
        self.assertTrue(summary["series_pass"])
        self.assertTrue(summary["threshold_gate"])
        self.assertTrue(summary["cold_wall_within_360s"])
        self.assertFalse(summary["certified_performance"])

    def test_summarize_series_fails_over_wall(self):
        baseline = {
            "series_pass": True,
            "decision": {"threshold_gate": True},
            "thresholds": {"export_wall_ms_max": 360000},
            "summary": {"cold_median_export_wall_ms": 400000.0},
            "runs": [
                {
                    "cache_mode": "cold",
                    "measurement_complete": True,
                    "export_wall_ms": 400000.0,
                }
                for _ in range(3)
            ],
        }
        summary = gate.summarize_series(baseline)
        self.assertFalse(summary["cold_wall_within_360s"])

    def test_summarize_series_all_cold_trials(self):
        baseline = {
            "series_pass": True,
            "decision": {"threshold_gate": True},
            "thresholds": {"export_wall_ms_max": 360000},
            "summary": {"cold_median_export_wall_ms": 110000.0},
            "runs": [
                {
                    "cache_mode": "cold",
                    "measurement_complete": True,
                    "export_wall_ms": 100000.0 + i,
                }
                for i in range(3)
            ],
        }
        summary = gate.summarize_series(baseline)
        self.assertTrue(summary["cold_wall_within_360s"])

    def test_run_ctest_probe_parses_missing(self):
        with mock.patch("p5_phase_gate_report.subprocess.run") as run:
            run.return_value = mock.Mock(
                returncode=0, stdout="No tests were found!!!\n", stderr=""
            )
            result = gate.run_ctest_probe(Path("build/release"), "does_not_exist")
        self.assertFalse(result["found"])
        self.assertFalse(result["passed"])


if __name__ == "__main__":
    unittest.main()
