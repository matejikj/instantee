"""Behavioral checks for the V1 release certification dossier assembler."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import v1_certification_dossier as dossier

REPO = Path(__file__).resolve().parents[2]


class V1CertificationDossierTest(unittest.TestCase):
    def test_assemble_accepts_current_repository_evidence(self):
        result = dossier.assemble_dossier()
        self.assertTrue(result["accepted"], result.get("blocking_defects"))
        self.assertTrue(result["product_v1_certificate"])
        self.assertFalse(result["git_tag_created"])
        self.assertFalse(result["certified_performance"])
        self.assertEqual(result["measurement"], "none")
        self.assertTrue(result["phase_gates"]["prior_phases_pass"])
        self.assertTrue(result["runtime_audits"]["pass"])
        self.assertTrue(all(item["pass"] for item in result["checklist"]))
        self.assertFalse(result["unexplained_missing_metrics"])
        self.assertIn(
            "The P8 phase gate and Git tag v1.0.0 remain a separate release step",
            " ".join(result["known_limitations"]),
        )

    def test_missing_checklist_evidence_fails(self):
        tracker = json.loads((REPO / "IMPLEMENTATION_STATUS.json").read_text())
        # Drop a required audit so the dossier must reject.
        tracker["tasks"]["P8-041"]["status"] = "todo"
        tracker["tasks"]["P8-041"]["acceptance"]["met"] = False
        with tempfile.TemporaryDirectory(prefix="instantee-v1-dossier-") as root:
            tracker_path = Path(root) / "tracker.json"
            tracker_path.write_text(json.dumps(tracker), encoding="utf-8")
            # Point coverage at the real matrix; only tracker is forged.
            result = dossier.assemble_dossier(tracker_path=tracker_path)
            self.assertFalse(result["accepted"])
            self.assertTrue(result["blocking_defects"])
            self.assertFalse(result["runtime_audits"]["pass"])

    def test_prior_phase_gate_regression_fails(self):
        tracker = json.loads((REPO / "IMPLEMENTATION_STATUS.json").read_text())
        tracker["phase_gates"]["P7"]["status"] = "todo"
        tracker["phase_gates"]["P7"]["decision"] = None
        with tempfile.TemporaryDirectory(prefix="instantee-v1-gates-") as root:
            tracker_path = Path(root) / "tracker.json"
            tracker_path.write_text(json.dumps(tracker), encoding="utf-8")
            result = dossier.assemble_dossier(tracker_path=tracker_path)
            self.assertFalse(result["accepted"])
            self.assertIn("prior_phase_gate_incomplete", result["blocking_defects"])

    def test_cli_writes_dossier_and_checklist(self):
        with tempfile.TemporaryDirectory(prefix="instantee-v1-out-") as root:
            report = Path(root) / "dossier.json"
            checklist = Path(root) / "checklist.json"
            code = dossier.main(["--report", str(report), "--checklist", str(checklist)])
            self.assertEqual(code, 0)
            loaded = json.loads(report.read_text(encoding="utf-8"))
            signed = json.loads(checklist.read_text(encoding="utf-8"))
            self.assertTrue(loaded["accepted"])
            self.assertTrue(signed["signed"])
            self.assertEqual(len(signed["items"]), len(dossier.CHECKLIST))
            self.assertTrue(all(item["pass"] for item in signed["items"]))


if __name__ == "__main__":
    unittest.main()
