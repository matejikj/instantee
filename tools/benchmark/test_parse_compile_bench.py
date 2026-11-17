"""Smoke checks for the parse/compile CLI benchmark harness."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import parse_compile_bench as bench

ROOT = Path(__file__).resolve().parents[2]


class ParseCompileBenchTests(unittest.TestCase):
    def test_manifest_sizes_and_duration(self):
        small = bench.build_manifest(1800)
        self.assertEqual(sum(s["duration_frames"] for s in small["shots"]), 1800)
        encoded = json.dumps(small, separators=(",", ":")).encode("utf-8")
        self.assertLess(len(encoded), 100_000)

        near_limit = bench.build_manifest(1800, target_bytes=200_000)
        near_bytes = json.dumps(near_limit, separators=(",", ":")).encode("utf-8")
        self.assertGreaterEqual(len(near_bytes), 200_000)

    def test_nearest_rank_and_summarize(self):
        self.assertEqual(bench.nearest_rank([1.0, 2.0, 3.0, 4.0], 50), 2.0)
        summary = bench.summarize([1.0, 2.0, 3.0])
        self.assertEqual(summary["sample_count"], 3)
        self.assertEqual(summary["unit"], "ms")

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

    def test_run_validate_parses_stage_report(self):
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
            mock.patch("parse_compile_bench.subprocess.Popen", return_value=FakeProc()),
            mock.patch(
                "parse_compile_bench.os.wait4",
                return_value=(4242, 0, FakeUsage()),
            ),
            mock.patch(
                "parse_compile_bench.os.waitstatus_to_exitcode", return_value=0
            ),
            tempfile.TemporaryDirectory() as tmp,
        ):
            manifest = Path(tmp) / "a.json"
            manifest.write_text("{}", encoding="utf-8")
            result = bench.run_validate(Path("/usr/bin/true"), manifest)
        self.assertEqual(result["ir_bytes"], 100)
        self.assertEqual(result["stages_ms"]["compile"], 2.0)
        self.assertEqual(result["rss_peak_bytes"], 4096)


if __name__ == "__main__":
    unittest.main()
