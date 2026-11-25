"""Behavioral checks for the V1 release gate assembler (no remote push)."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import v1_release_gate as gate

REPO = Path(__file__).resolve().parents[2]


class V1ReleaseGateTest(unittest.TestCase):
    def test_all_p8_tasks_detect_open_gate_task(self):
        tracker = json.loads((REPO / "IMPLEMENTATION_STATUS.json").read_text())
        open_report = gate.all_p8_tasks_done(tracker, allow_open={"P8-037"})
        self.assertTrue(open_report["pass"], open_report)
        closed = gate.all_p8_tasks_done(tracker, allow_open=set())
        # While this delivery is in progress the gate task itself may be open.
        if tracker["tasks"]["P8-037"]["status"] != "done":
            self.assertFalse(closed["pass"])
            self.assertIn("P8-037", closed["incomplete"])

    def test_assemble_report_rejects_bad_smoke(self):
        tracker = json.loads((REPO / "IMPLEMENTATION_STATUS.json").read_text())
        package = {
            "version": "1.0.0",
            "package_dir": "build/dist/macos/instantee-macos-1.0.0-arm64",
            "archive_sha256": "a" * 64,
        }
        with tempfile.TemporaryDirectory(prefix="instantee-v1-gate-") as root:
            tracker_path = Path(root) / "tracker.json"
            tracker_path.write_text(json.dumps(tracker), encoding="utf-8")
            report = gate.assemble_report(
                package=package,
                smoke={"ok": False},
                tracker_path=tracker_path,
                allow_open={"P8-037"},
            )
            self.assertFalse(report["accepted"])
            self.assertIn("package_smoke_failed", report["blocking_defects"])

    def test_create_tag_refuses_existing(self):
        with mock.patch("v1_release_gate.subprocess.run") as run:
            run.return_value = mock.Mock(returncode=0, stdout="abc\n", stderr="")
            with self.assertRaises(RuntimeError):
                gate.create_annotated_tag(message="nope")


if __name__ == "__main__":
    unittest.main()
