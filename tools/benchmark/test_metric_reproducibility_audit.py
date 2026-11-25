"""Behavioral checks for the metric reproducibility audit."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from measure import nearest_rank
from metric_reproducibility_audit import (
    DEFAULT_SERIES,
    audit_completeness_and_loss,
    audit_independent_series,
    audit_oracle_agreement,
    audit_report_pass_gates,
    audit_telemetry_overhead,
    overflow_safe_nearest_rank,
    run_audit,
)


ROOT = Path(__file__).resolve().parents[2]


class MetricReproducibilityAuditTest(unittest.TestCase):
    def test_overflow_safe_matches_python_nearest_rank(self):
        for values in (list(range(1, 101)), list(range(1, 10_001)), [0, 1, 999]):
            with self.subTest(n=len(values)):
                left = nearest_rank(values)
                right = overflow_safe_nearest_rank(values)
                self.assertIsNotNone(left)
                self.assertIsNotNone(right)
                for key in ("min", "p50", "p95", "p99", "max", "sample_count"):
                    self.assertEqual(left[key], right[key])

    def test_oracle_section_passes(self):
        self.assertTrue(audit_oracle_agreement()["pass"])

    def test_completeness_rejects_loss(self):
        self.assertTrue(audit_completeness_and_loss()["pass"])

    def test_report_gates_reject_fixture_loss_and_missing(self):
        result = audit_report_pass_gates()
        self.assertTrue(result["pass"])
        self.assertTrue(result["schema_fixture_not_eligible"])
        self.assertTrue(result["lost_samples_not_eligible"])
        self.assertTrue(result["missing_metrics_not_eligible"])
        self.assertTrue(result["units_intervals_profiles_explicit"])

    def test_independent_series_recomputes_committed_summaries(self):
        result = audit_independent_series(DEFAULT_SERIES)
        self.assertTrue(result["pass"])
        self.assertEqual(result["measurement"], "none")
        for style in ("B-01", "B-02", "B-03"):
            with self.subTest(style=style):
                self.assertTrue(result["styles"][style]["pass"])
                self.assertTrue(
                    result["styles"][style]["summary_matches_independent_recompute"]
                )
                self.assertGreaterEqual(result["styles"][style]["cold_repeats"], 3)
                self.assertGreaterEqual(result["styles"][style]["warm_repeats"], 3)

    def test_tampered_series_summary_fails_recompute(self):
        path = DEFAULT_SERIES["B-01"]
        baseline = json.loads(path.read_text(encoding="utf-8"))
        baseline["summary"]["cold_median_export_wall_ms"] = 1.0
        with tempfile.TemporaryDirectory(prefix="instantee-metric-audit-") as root:
            forged = Path(root) / "b01.json"
            forged.write_text(json.dumps(baseline), encoding="utf-8")
            result = audit_independent_series({"B-01": forged, **{
                key: DEFAULT_SERIES[key] for key in ("B-02", "B-03")
            }})
            self.assertFalse(result["styles"]["B-01"]["pass"])
            self.assertFalse(
                result["styles"]["B-01"]["summary_matches_independent_recompute"]
            )

    def test_overhead_is_not_performance_pass(self):
        overhead = audit_telemetry_overhead(
            ROOT / "implementation-evidence/P0-016/overhead/overhead.json"
        )
        self.assertTrue(overhead["pass"])
        self.assertTrue(overhead["not_performance_pass"])

    def test_run_audit_writes_pass_report(self):
        with tempfile.TemporaryDirectory(prefix="instantee-metric-audit-out-") as root:
            report = Path(root) / "audit-report.json"
            result = run_audit()
            self.assertTrue(result["pass"])
            self.assertFalse(result["certified_performance"])
            report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
            loaded = json.loads(report.read_text(encoding="utf-8"))
            self.assertTrue(loaded["pass"])


if __name__ == "__main__":
    unittest.main()
