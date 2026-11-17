"""Smoke checks for the CDS compile benchmark harness."""

from __future__ import annotations

import json
import unittest
from pathlib import Path
from unittest import mock

import cds_compile_bench as bench

ROOT = Path(__file__).resolve().parents[2]


class CdsCompileBenchTests(unittest.TestCase):
    def test_manifest_has_transitions_and_duration(self):
        doc = bench.build_manifest("editorial@1", 1800)
        self.assertEqual(sum(s["duration_frames"] for s in doc["shots"]), 1800)
        self.assertEqual(len(doc["transitions"]), len(doc["shots"]) - 1)
        self.assertEqual(doc["style"], "editorial@1")

    def test_machine_profile_allowlist(self):
        profile = bench.machine_profile()
        allowed = {
            "os",
            "os_release",
            "arch",
            "python",
            "logical_cpus",
            "page_size",
            "certification",
        }
        self.assertEqual(set(profile), allowed)
        self.assertNotIn("hostname", profile)

    def test_budget_exceedance_is_risk_not_pass(self):
        case = {
            "style": "editorial",
            "class": "typical_60s",
            "cold": {
                "wall_ms": {"p95": 80.0, "p50": 70.0},
                "compile_ms": {"p50": 40.0},
                "cds": {"total": {"p50": 30.0}},
                "rss_peak_bytes": {"p95": 10 * 1024 * 1024},
            },
        }
        rows = bench.evaluate_budgets([case])
        self.assertEqual(rows[0]["status"], "exceeds_draft_budget")
        self.assertEqual(rows[0]["exceedances"][0]["metric"], "cold_wall_p95_ms")
        self.assertAlmostEqual(rows[0]["cds_share_of_compile_p50"], 0.75)

    def test_run_validate_requires_cds_breakdown(self):
        payload = {
            "ok": True,
            "command": "validate",
            "duration_frames": 30,
            "stages_ms": {
                "parse": 1.0,
                "structural": 1.0,
                "compile": 2.0,
                "graph": 2.0,
                "canonicalize": 3.0,
                "write": 0.5,
                "cds": {
                    "style_resolve": 0.2,
                    "shot_planning": 0.8,
                    "transition_planning": 0.3,
                    "lint": 0.4,
                    "direction_report": 0.1,
                    "total": 1.8,
                },
            },
            "ir_bytes": 100,
        }

        class FakeUsage:
            ru_maxrss = 4096
            ru_utime = 0.01
            ru_stime = 0.02

        class FakeProc:
            pid = 4242

            def __init__(self):
                self.stdout = mock.Mock()
                self.stdout.read.return_value = json.dumps(payload).encode("utf-8")
                self.stderr = mock.Mock()
                self.stderr.read.return_value = b""

        with (
            mock.patch("cds_compile_bench.subprocess.Popen", return_value=FakeProc()),
            mock.patch(
                "cds_compile_bench.os.wait4",
                return_value=(4242, 0, FakeUsage()),
            ),
        ):
            sample = bench.run_validate(
                Path("instantee"), Path("a.json"), Path("b.style.json")
            )
        self.assertIn("cds", sample["stages_ms"])
        self.assertEqual(sample["stages_ms"]["cds"]["total"], 1.8)

    def test_style_bibles_exist(self):
        for style in bench.STYLES:
            path = ROOT / style["bible"]
            self.assertTrue(path.is_file(), msg=str(path))


if __name__ == "__main__":
    unittest.main()
