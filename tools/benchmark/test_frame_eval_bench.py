"""Unit checks for frame-eval benchmark report packing helpers."""

from __future__ import annotations

import unittest

from frame_eval_bench import growth_analysis, summarize_benchmarks


class FrameEvalBenchTests(unittest.TestCase):
    def test_growth_plateau_passes(self) -> None:
        benches = [
            {
                "name": f"BM_AllocationGrowthProbe/{batch}",
                "counters": {
                    "batch_frames": float(batch),
                    "rss_before_bytes": 10_000_000.0,
                    "rss_after_bytes": 10_000_000.0 + 50_000.0,
                },
            }
            for batch in (256, 512, 1024, 2048)
        ]
        growth = growth_analysis(benches)
        self.assertEqual(growth["verdict"], "pass")
        self.assertFalse(growth["unbounded_per_frame_growth"])

    def test_growth_climb_fails(self) -> None:
        benches = []
        rss = 10_000_000.0
        for batch in (256, 512, 1024, 2048):
            # Pathological: delta grows with batch squared.
            delta = float(batch * batch * 64)
            benches.append(
                {
                    "name": f"BM_AllocationGrowthProbe/{batch}",
                    "counters": {
                        "batch_frames": float(batch),
                        "rss_before_bytes": rss,
                        "rss_after_bytes": rss + delta,
                    },
                }
            )
        growth = growth_analysis(benches)
        self.assertEqual(growth["verdict"], "fail")
        self.assertTrue(growth["unbounded_per_frame_growth"])

    def test_summarize_skips_aggregates(self) -> None:
        raw = {
            "benchmarks": [
                {
                    "name": "BM_EvaluateFrameSequential/1024/32",
                    "run_type": "iteration",
                    "iterations": 10,
                    "real_time": 1.0,
                    "cpu_time": 1.0,
                    "time_unit": "us",
                    "counters": {"nodes": {"value": 32}},
                },
                {
                    "name": "BM_EvaluateFrameSequential/1024/32_mean",
                    "run_type": "aggregate",
                    "iterations": 10,
                },
            ]
        }
        summarized = summarize_benchmarks(raw)
        self.assertEqual(len(summarized), 1)
        self.assertEqual(summarized[0]["counters"]["nodes"], 32.0)

    def test_summarize_reads_flattened_counters(self) -> None:
        # Google Benchmark JSON emits UserCounters as top-level fields.
        raw = {
            "benchmarks": [
                {
                    "name": "BM_SchedulerPipeline/64/1",
                    "run_type": "iteration",
                    "iterations": 3,
                    "real_time": 1.0,
                    "cpu_time": 1.0,
                    "time_unit": "ms",
                    "blocked_wait_ns": 4200.0,
                    "queue_high_water": 1.0,
                    "blocked_push": 2.0,
                }
            ]
        }
        summarized = summarize_benchmarks(raw)
        self.assertEqual(summarized[0]["counters"]["blocked_wait_ns"], 4200.0)
        self.assertEqual(summarized[0]["counters"]["queue_high_water"], 1.0)


if __name__ == "__main__":
    unittest.main()
