"""Behavioral checks for the B-04/B-05 thermal endurance suite helpers."""

from __future__ import annotations

import unittest

from style_endurance import (
    FAMILY_SPECS,
    RSS_HARD_BUDGET_BYTES,
    SLOWDOWN_RATIO_MAX,
    analyze_fps_and_slowdown,
    assemble_suite,
    render_ascii_plot,
    working_set_within_budget,
)


def _frames(count: int = 1200, *, late_latency_ns: int = 40_000_000) -> list[list[int]]:
    rows: list[list[int]] = []
    t = 0
    for index in range(count):
        latency = 40_000_000 if index < count // 2 else late_latency_ns
        request = t
        accepted = t + latency
        rows.append(
            [
                index,
                request,
                request,
                request,
                request,
                request,
                request,
                request,
                accepted,
            ]
        )
        t = accepted + 1_000_000
    return rows


class StyleEnduranceHelpersTest(unittest.TestCase):
    def test_family_specs_locked(self):
        self.assertEqual(FAMILY_SPECS["B-04"]["workload_id"], "B-04-mixed-300s-v2")
        self.assertEqual(
            FAMILY_SPECS["B-05"]["workload_id"], "B-05-worst-accepted-300s-v2"
        )
        self.assertEqual(FAMILY_SPECS["B-04"]["frames"], 9000)

    def test_fps_series_detects_fatal_slowdown(self):
        ok = analyze_fps_and_slowdown(_frames(late_latency_ns=40_000_000))
        self.assertIsNotNone(ok)
        assert ok is not None
        self.assertFalse(ok["fatal_slowdown"])
        self.assertGreater(len(ok["fps_buckets"]), 0)

        bad = analyze_fps_and_slowdown(
            _frames(late_latency_ns=int(40_000_000 * (SLOWDOWN_RATIO_MAX + 1)))
        )
        self.assertIsNotNone(bad)
        assert bad is not None
        self.assertTrue(bad["fatal_slowdown"])
        self.assertGreater(bad["late_over_early_latency_ratio"], SLOWDOWN_RATIO_MAX)

    def test_working_set_budget_gates(self):
        series = {
            "rss_peak_max_bytes": 100_000_000,
            "post_warmup_rss_projected_growth_bytes": 1_000,
        }
        ok, reason = working_set_within_budget(series, 268_435_456)
        self.assertTrue(ok)
        self.assertEqual(reason, "within_budget")

        over_peak = {
            "rss_peak_max_bytes": 300_000_000,
            "post_warmup_rss_projected_growth_bytes": 1_000,
        }
        ok, reason = working_set_within_budget(over_peak, 268_435_456)
        self.assertFalse(ok)
        self.assertEqual(reason, "declared_rss_budget_exceeded")

        hard = {
            "rss_peak_max_bytes": RSS_HARD_BUDGET_BYTES + 1,
            "post_warmup_rss_projected_growth_bytes": 0,
        }
        ok, reason = working_set_within_budget(hard, RSS_HARD_BUDGET_BYTES + 10)
        self.assertFalse(ok)
        self.assertEqual(reason, "rss_hard_budget_exceeded")

    def test_ascii_plot_and_suite_assembly(self):
        lines = render_ascii_plot(
            [{"fps": 10.0}, {"fps": 20.0}, {"fps": 15.0}, {"fps": 12.0}]
        )
        self.assertTrue(any("*" in line for line in lines))

        family = {
            "series_pass": True,
            "workload_id": FAMILY_SPECS["B-04"]["workload_id"],
            "cold_pass_count": 1,
            "warm_pass_count": 1,
            "declared_budgets": {"rss_declared_peak_budget_bytes": 1},
            "runs": [
                {
                    "run_id": "cold-00",
                    "cache_mode": "cold",
                    "pass": True,
                    "export_wall_ms": 1,
                    "effective_fps": 20.0,
                    "rss_peak_bytes": 1,
                    "in_flight_peak": 1,
                    "queue_peak": 1,
                    "encoder_pool_peak": 8,
                    "working_set_reason": "within_budget",
                    "fps_series": {
                        "fatal_slowdown": False,
                        "late_over_early_latency_ratio": 1.0,
                    },
                    "thermal_states_observed": ["nominal"],
                    "error_count": {
                        "failed_frames": 0,
                        "cancelled_frames": 0,
                        "frame_lost_samples": 0,
                        "resource_lost_samples": 0,
                    },
                    "resource_series": {
                        "sample_count": 10,
                        "rss_peak_max_bytes": 1,
                        "rss_current_max_bytes": 1,
                        "post_warmup_rss_projected_growth_bytes": 0,
                    },
                }
            ],
        }
        suite = assemble_suite({"B-04": family, "B-05": {**family, "workload_id": FAMILY_SPECS["B-05"]["workload_id"]}})
        self.assertTrue(suite["pass"])
        self.assertFalse(suite["performance_claim"])
        self.assertEqual(
            suite["evidence_kind"], "b04_b05_300s_thermal_endurance_suite"
        )


if __name__ == "__main__":
    unittest.main()
