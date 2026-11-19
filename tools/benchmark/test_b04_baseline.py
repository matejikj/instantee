"""Behavioral checks for the B-04 mixed reference-series harness."""

from __future__ import annotations

import copy
import unittest

from b04_baseline import (
    B04_FRAMES,
    B04_QUALITY_TIER,
    B04_WORKLOAD_ID,
    measurement_complete,
    thresholds_pass,
    trial_pass,
)


def _report(frames: int = B04_FRAMES) -> dict:
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
            "export_wall_ms": 340000.0,
            "effective_fps": 26.5,
            "frame_latency_ns": {"sample_count": frames, "p50": 1, "p99": 50_000_000},
            "cpu": {
                "scope": "renderer_process_all_threads",
                "seconds_per_verified_frame": 0.04,
            },
            "memory": {"rss_peak_bytes": 128_000_000},
        },
    }


class B04BaselineHarnessTest(unittest.TestCase):
    def test_locked_identity(self):
        self.assertEqual(B04_WORKLOAD_ID, "B-04-mixed-300s-v2")
        self.assertEqual(B04_FRAMES, 9000)
        self.assertEqual(B04_QUALITY_TIER, "mixed-b04-v1")

    def test_complete_measurement_and_threshold_gate(self):
        report = _report()
        thresholds = {
            "export_wall_ms_max": 1_800_000,
            "frame_latency_p99_ns_max": 280_000_000,
            "rss_peak_bytes_max": 268_435_456,
            "cpu_seconds_per_verified_frame_max": 0.18,
        }
        self.assertTrue(measurement_complete(report, B04_FRAMES))
        self.assertEqual(thresholds_pass(report, thresholds)[0], True)
        self.assertTrue(trial_pass(report, B04_FRAMES, thresholds))

    def test_null_threshold_fails(self):
        report = _report()
        thresholds = {
            "export_wall_ms_max": 1_800_000,
            "frame_latency_p99_ns_max": None,
            "rss_peak_bytes_max": 268_435_456,
            "cpu_seconds_per_verified_frame_max": 0.18,
        }
        ok, reason = thresholds_pass(report, thresholds)
        self.assertFalse(ok)
        self.assertEqual(reason, "threshold_null")

    def test_wall_exceeded_fails(self):
        report = _report()
        report["metrics"]["export_wall_ms"] = 1_800_001
        thresholds = {
            "export_wall_ms_max": 1_800_000,
            "frame_latency_p99_ns_max": 280_000_000,
            "rss_peak_bytes_max": 268_435_456,
            "cpu_seconds_per_verified_frame_max": 0.18,
        }
        self.assertFalse(trial_pass(report, B04_FRAMES, thresholds))

    def test_incomplete_frames_fail(self):
        report = _report()
        incomplete = copy.deepcopy(report)
        incomplete["frames"]["verified"] = B04_FRAMES - 1
        self.assertFalse(measurement_complete(incomplete, B04_FRAMES))


if __name__ == "__main__":
    unittest.main()
