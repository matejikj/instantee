"""Behavioral checks for the B-05 worst-accepted reference-series harness."""

from __future__ import annotations

import copy
import unittest

from b05_baseline import (
    B05_COMPLEXITY_TIER,
    B05_FRAMES,
    B05_QUALITY_TIER,
    B05_WORKLOAD_ID,
    measurement_complete,
    thresholds_pass,
    trial_pass,
)


def _report(frames: int = B05_FRAMES) -> dict:
    return {
        "result": "success",
        "certified": False,
        "evidence_kind": "measured",
        "child_exit_code": 0,
        "frames": {
            "scheduled": frames,
            "admitted": frames,
            "accepted": frames,
            "verified": frames,
            "failed": 0,
            "cancelled": 0,
        },
        "output": {"published": True},
        "metrics": {
            "export_wall_ms": 520000.0,
            "effective_fps": 17.3,
            "frame_latency_ns": {"sample_count": frames, "p50": 1, "p99": 80_000_000},
            "cpu": {
                "scope": "renderer_process_all_threads",
                "seconds_per_verified_frame": 0.06,
            },
            "memory": {"rss_peak_bytes": 180_000_000},
        },
    }


class B05BaselineHarnessTest(unittest.TestCase):
    def test_locked_identity(self):
        self.assertEqual(B05_WORKLOAD_ID, "B-05-worst-accepted-300s-v2")
        self.assertEqual(B05_FRAMES, 9000)
        self.assertEqual(B05_QUALITY_TIER, "worst-accepted-b05-v1")
        self.assertEqual(B05_COMPLEXITY_TIER, "worst-accepted-b05-v1")

    def test_complete_measurement_and_threshold_gate(self):
        report = _report()
        thresholds = {
            "export_wall_ms_max": 1_800_000,
            "frame_latency_p99_ns_max": 500_000_000,
            "rss_peak_bytes_max": 536_870_912,
            "cpu_seconds_per_verified_frame_max": 0.25,
        }
        self.assertTrue(measurement_complete(report, B05_FRAMES))
        self.assertEqual(thresholds_pass(report, thresholds)[0], True)
        self.assertTrue(trial_pass(report, B05_FRAMES, thresholds))

    def test_null_threshold_fails(self):
        report = _report()
        thresholds = {
            "export_wall_ms_max": 1_800_000,
            "frame_latency_p99_ns_max": None,
            "rss_peak_bytes_max": 536_870_912,
            "cpu_seconds_per_verified_frame_max": 0.25,
        }
        ok, reason = thresholds_pass(report, thresholds)
        self.assertFalse(ok)
        self.assertEqual(reason, "threshold_null")

    def test_wall_exceeded_fails(self):
        report = _report()
        report["metrics"]["export_wall_ms"] = 1_800_001
        thresholds = {
            "export_wall_ms_max": 1_800_000,
            "frame_latency_p99_ns_max": 500_000_000,
            "rss_peak_bytes_max": 536_870_912,
            "cpu_seconds_per_verified_frame_max": 0.25,
        }
        self.assertFalse(trial_pass(report, B05_FRAMES, thresholds))

    def test_incomplete_frames_fail(self):
        report = _report()
        incomplete = copy.deepcopy(report)
        incomplete["frames"]["verified"] = B05_FRAMES - 1
        self.assertFalse(measurement_complete(incomplete, B05_FRAMES))


if __name__ == "__main__":
    unittest.main()
