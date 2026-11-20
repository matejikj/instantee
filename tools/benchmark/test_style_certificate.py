"""Behavioral checks for the B-01/B-02/B-03 style certificate assembler."""

from __future__ import annotations

import copy
import unittest

from style_certificate import (
    STYLE_SPECS,
    assemble_certificate,
    thresholds_explicit,
    validate_style_series,
)


def _run(run_id: str, cache_mode: str, wall_ms: float = 100_000.0) -> dict:
    return {
        "run_id": run_id,
        "cache_mode": cache_mode,
        "pass": True,
        "measurement_complete": True,
        "export_wall_ms": wall_ms,
        "frame_latency_p99_ns": 50_000_000,
        "rss_peak_bytes": 120_000_000,
        "cpu_seconds_per_verified_frame": 0.05,
        "threshold_reason": "within_thresholds",
    }


def _baseline(style: str, *, wall_ms: float = 100_000.0) -> dict:
    spec = STYLE_SPECS[style]
    thresholds = {
        "source_adr": f"ADR-0014/{spec['quality_tier']}",
        "export_wall_ms_max": 360_000,
        "frame_latency_p99_ns_max": 280_000_000,
        "rss_peak_bytes_max": 268_435_456,
        "cpu_seconds_per_verified_frame_max": 0.18,
    }
    runs = [_run(f"cold-{i:02d}", "cold", wall_ms) for i in range(3)]
    runs += [_run(f"warm-{i:02d}", "warm", wall_ms) for i in range(3)]
    return {
        "evidence_kind": spec["evidence_kind"],
        "certified": False,
        "workload_id": spec["workload_id"],
        "frames_per_trial": spec["frames"],
        "profile_sha256": "a" * 64,
        "thresholds": thresholds,
        "series_pass": True,
        "cold_wall_within_360s": True,
        "runs": runs,
    }


class StyleCertificateTest(unittest.TestCase):
    def test_thresholds_null_fail(self):
        thresholds = {
            "export_wall_ms_max": 360_000,
            "frame_latency_p99_ns_max": None,
            "rss_peak_bytes_max": 268_435_456,
            "cpu_seconds_per_verified_frame_max": 0.18,
        }
        ok, reason = thresholds_explicit(thresholds)
        self.assertFalse(ok)
        self.assertEqual(reason, "threshold_null")

    def test_style_pass_and_wall_exceed(self):
        ok = validate_style_series("B-01", _baseline("B-01"))
        self.assertTrue(ok["pass"])
        self.assertTrue(ok["thresholds_explicit"])
        self.assertTrue(ok["cold_wall_within_360s"])

        exceeded = validate_style_series(
            "B-01", _baseline("B-01", wall_ms=360_001.0)
        )
        self.assertFalse(exceeded["pass"])
        self.assertIn("cold_wall_exceeded", exceeded["reasons"])

    def test_assemble_requires_all_styles_and_passes(self):
        certificate = assemble_certificate(
            {
                "B-01": _baseline("B-01"),
                "B-02": _baseline("B-02"),
                "B-03": _baseline("B-03"),
            }
        )
        self.assertTrue(certificate["pass"])
        self.assertFalse(certificate["run_reports_certified"])
        self.assertFalse(certificate["product_v1_certificate"])
        self.assertEqual(
            certificate["evidence_kind"], "b01_b02_b03_60s_style_certificate"
        )
        self.assertTrue(certificate["decision"]["threshold_gate"])

    def test_assemble_fails_when_one_style_fails(self):
        baselines = {
            "B-01": _baseline("B-01"),
            "B-02": _baseline("B-02", wall_ms=400_000.0),
            "B-03": _baseline("B-03"),
        }
        certificate = assemble_certificate(baselines)
        self.assertFalse(certificate["pass"])
        self.assertFalse(certificate["styles"]["B-02"]["pass"])

    def test_missing_style_raises(self):
        with self.assertRaises(ValueError):
            assemble_certificate({"B-01": _baseline("B-01"), "B-02": _baseline("B-02")})

    def test_null_threshold_in_baseline_fails_style(self):
        baseline = _baseline("B-03")
        baseline["thresholds"] = copy.deepcopy(baseline["thresholds"])
        baseline["thresholds"]["cpu_seconds_per_verified_frame_max"] = None
        result = validate_style_series("B-03", baseline)
        self.assertFalse(result["pass"])
        self.assertEqual(result["thresholds_reason"], "threshold_null")


if __name__ == "__main__":
    unittest.main()
