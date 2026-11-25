"""Crash-visibility matrix for output publication phases.

SIGKILL proves process-crash atomic visibility only. It is explicitly not
power-loss or media-durability evidence.
"""

from __future__ import annotations

import json
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import tempfile
import unittest

CHILD = sys.argv[1]
REPORT = Path(sys.argv[2]) if len(sys.argv) > 2 else None
del sys.argv[1:]


PRE_PUBLISH = {"finish", "file_sync", "rename"}
POST_PUBLISH = {"metadata_sync", "report", "published"}
PHASES = ("finish", "file_sync", "rename", "metadata_sync", "report",
          "published")


class CrashConsistencyMatrix(unittest.TestCase):
    cases: list[dict] = []

    def start(self, phase: str, target: Path):
        child = subprocess.Popen(
            [CHILD, phase, str(target)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.addCleanup(self.reap, child)
        with selectors.DefaultSelector() as selector:
            selector.register(child.stdout, selectors.EVENT_READ)
            self.assertTrue(selector.select(30), f"ready timeout for {phase}")
        self.assertEqual(child.stdout.readline().strip(), "READY")
        return child

    @staticmethod
    def reap(child):
        if child.poll() is None:
            child.kill()
        child.communicate(timeout=10)
        child.stdout.close()
        child.stderr.close()

    def observe(self, phase: str, existing: bool):
        with tempfile.TemporaryDirectory(prefix="instantee-crash-") as root:
            root_path = Path(root)
            target = root_path / "target.mp4"
            previous = b"previous output"
            if existing:
                target.write_bytes(previous)
            child = self.start(phase, target)
            child.kill()
            child.communicate(timeout=10)
            self.assertEqual(child.returncode, -signal.SIGKILL)

            after_exists = target.exists()
            after_bytes = target.read_bytes() if after_exists else None
            private = [
                p for p in root_path.iterdir()
                if p != target and p.name.startswith(".instantee-txn-")
            ]

            if phase in PRE_PUBLISH:
                if existing:
                    self.assertEqual(after_bytes, previous)
                else:
                    self.assertFalse(after_exists)
                for artifact in private:
                    self.assertTrue(artifact.is_dir())
                    self.assertEqual(artifact.stat().st_mode & 0o777, 0o700)
            else:
                self.assertTrue(after_exists)
                self.assertEqual(after_bytes[4:8], b"ftyp")
                if existing:
                    self.assertNotEqual(after_bytes, previous)

            self.cases.append({
                "phase": phase,
                "signal": "SIGKILL",
                "existing_target": existing,
                "target_published": phase in POST_PUBLISH,
                "target_unchanged_when_pre_publish": phase in PRE_PUBLISH,
                "private_staging_only_when_pre_publish": (
                    phase in PRE_PUBLISH and len(private) <= 1
                ),
                "valid_ftyp_when_post_publish": phase in POST_PUBLISH,
            })

    def test_sigkill_matrix(self):
        for phase in PHASES:
            for existing in (False, True):
                with self.subTest(phase=phase, existing=existing):
                    self.observe(phase, existing)

    @classmethod
    def tearDownClass(cls):
        if REPORT is None:
            return
        REPORT.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "schema_version": 1,
            "subject": "output publish crash-visibility matrix",
            "sigkill_is_power_loss_evidence": False,
            "power_loss_claim": (
                "SIGKILL and process-crash tests do not certify power-loss "
                "or media durability; durable-local requires F_FULLFSYNC on "
                "APFS/HFS+ and a separate storage-fault experiment."
            ),
            "cases": cls.cases,
        }
        REPORT.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    unittest.main()
