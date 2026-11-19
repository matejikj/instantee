"""Independent metric oracles and native sampler/export integration checks."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from measure import (PROFILE_KEYS, cpu_metrics, derive_metrics, nearest_rank,
                     validate_profile, wait_for_export)
from baseline import B00_FRAMES, B00_WORKLOAD_ID, trial_pass
from endurance import (B00_300_FRAMES, B00_300_WORKLOAD_ID, analyze_resource_series,
                       endurance_pass, oldest_slow_frame)


def _optional_binary() -> str | None:
    # ctest passes the metrics binary as argv[1]. unittest discover must not
    # treat its own subcommand name as that executable path.
    if len(sys.argv) < 2 or sys.argv[1].startswith("-"):
        return None
    candidate = Path(sys.argv[1])
    if candidate.is_file() and os.access(candidate, os.X_OK):
        return sys.argv.pop(1)
    return None


BINARY = _optional_binary()
ROOT = Path(__file__).resolve().parents[2]


def fixture():
    samples = [[i, i * 1000, i * 1000 + 200, i * 1000 + 201, i * 1000 + 202,
                i * 1000 + 203, i * 1000 + 204, i * 1000 + 205, i * 1000 + 900]
               for i in range(3)]
    child = dict(telemetry_enabled=True, frame_lost_samples=0, resource_lost_samples=0,
                 latency_summary=[3, 900, 900, 900, 900, 900], published=True,
                 scheduled=3, admitted=3, accepted=3, verified=3,
                 in_flight_peak=2, queue_peak=1, encoder_pool_peak=8, finalize_ms=2.0)
    resources = [[0, 10, 5, 1024, 2048, 512], [100, 20, 10, 2048, 2048, 1024]]
    outer = dict(exit_code=0, timed_out=False, wall_ns=2_000_000_000,
                 user_ns=3_000_000_000, system_ns=1_000_000_000, rss_peak_bytes=4096)
    return child, samples, resources, outer


class MetricOracleTests(unittest.TestCase):
    def test_invalid_profile_shapes_are_rejected_before_spawn(self):
        for profile in (None, [], "profile", {}, {section: None for section in PROFILE_KEYS},
                        {section: [] for section in PROFILE_KEYS}):
            with self.subTest(profile_type=type(profile).__name__):
                with self.assertRaises(ValueError):
                    validate_profile(profile)
        profile = {section: dict.fromkeys(keys) for section, keys in PROFILE_KEYS.items()}
        profile["machine_profile"]["unapproved_field"] = "private-input-do-not-echo"
        with self.assertRaises(ValueError):
            validate_profile(profile)

    def test_cli_errors_do_not_echo_supplied_input(self):
        marker = "private-input-do-not-echo"
        for program in ("measure.py", "compare.py", "baseline.py", "endurance.py"):
            result = subprocess.run([sys.executable, str(Path(__file__).parent / program),
                                     "--unknown-option", marker], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 2)
            self.assertNotIn(marker, result.stderr)
            self.assertEqual(json.loads(result.stderr)["code"], "invalid_measurement_arguments")

    def test_endurance_series_helpers_reject_growth_and_find_oldest_slow_frame(self):
        resources = [
            [0, 10, 5, 1000, 1000, 900],
            [100, 20, 10, 1100, 1100, 950],
            [200, 30, 15, 1200, 1200, 1000],
            [300, 40, 20, 1300, 1300, 1050],
            [400, 50, 25, 1400, 1400, 1100],
        ]
        series = analyze_resource_series(resources, warmup_fraction=0.2)
        self.assertEqual(series["sample_count"], 5)
        self.assertGreater(series["post_warmup_rss_slope_bytes_per_ns"], 0)
        frames = [
            [0, 0, 1, 2, 3, 4, 5, 6, 100],
            [1, 10, 11, 12, 13, 14, 15, 16, 210],
            [2, 20, 21, 22, 23, 24, 25, 26, 220],
        ]
        slow = oldest_slow_frame(frames)
        self.assertEqual(slow, {"frame_index": 1, "latency_ns": 200})
        self.assertEqual(B00_300_FRAMES, 9000)
        self.assertEqual(B00_300_WORKLOAD_ID, "B-00-test-pattern-300s-v1")
        report = {
            "result": "success", "certified": False, "evidence_kind": "measured",
            "child_exit_code": 0,
            "frames": {"verified": 9000, "scheduled": 9000, "admitted": 9000,
                       "accepted": 9000, "failed": 0, "cancelled": 0},
            "metrics": {
                "frame_latency_ns": {"sample_count": 9000, "p99": 1},
                "effective_fps": 30.0,
                "cpu": {"scope": "renderer_process_all_threads"},
                "memory": {"rss_peak_bytes": 1000},
            },
            "output": {"published": True},
        }
        child = {"in_flight_peak": 2, "queue_peak": 1, "encoder_pool_peak": 8,
                 "frame_lost_samples": 0, "resource_lost_samples": 0}
        flat = [[i * 10, i, i, 1000, 1000, 900] for i in range(10)]
        self.assertTrue(endurance_pass(report, 9000, child, flat, frames))
        leaky = [[i * 10, i, i, i * (512 * 1024 * 1024), i * (512 * 1024 * 1024), 900]
                 for i in range(10)]
        self.assertFalse(endurance_pass(report, 9000, child, leaky, frames))
        over_cap = dict(child, queue_peak=2)
        self.assertFalse(endurance_pass(report, 9000, over_cap, flat, frames))

    def test_baseline_pass_requires_complete_verified_samples(self):
        report = {
            "result": "success", "certified": False, "evidence_kind": "measured",
            "child_exit_code": 0,
            "frames": {"scheduled": B00_FRAMES, "admitted": B00_FRAMES, "accepted": B00_FRAMES,
                       "verified": B00_FRAMES, "failed": 0, "cancelled": 0},
            "output": {"published": True},
            "metrics": {
                "effective_fps": 20.0,
                "frame_latency_ns": {"sample_count": B00_FRAMES, "p99": 1},
                "cpu": {"scope": "renderer_process_all_threads",
                        "seconds_per_verified_frame": 0.01},
                "memory": {"rss_peak_bytes": 1024},
            },
        }
        self.assertTrue(trial_pass(report, B00_FRAMES))
        incomplete = copy.deepcopy(report)
        incomplete["frames"]["verified"] = B00_FRAMES - 1
        self.assertFalse(trial_pass(incomplete, B00_FRAMES))
        missing_latency = copy.deepcopy(report)
        missing_latency["metrics"]["frame_latency_ns"] = None
        self.assertFalse(trial_pass(missing_latency, B00_FRAMES))
        self.assertEqual(B00_WORKLOAD_ID, "B-00-test-pattern-60s-v1")

    def test_exact_nearest_rank_and_zero_samples(self):
        self.assertIsNone(nearest_rank([]))
        result = nearest_rank(list(range(1, 101)))
        self.assertEqual([result[k] for k in ("min", "p50", "p95", "p99", "max")], [1, 50, 95, 99, 100])
        self.assertEqual(nearest_rank([0, 1, 999])["p99"], 999)

    def test_cpu_normalization_and_missing_denominator(self):
        value = cpu_metrics(3_000_000_000, 1_000_000_000, 2_000_000_000, 8, 10)
        self.assertEqual(value["core_equivalents"], 2)
        self.assertEqual(value["percent_one_core"], 200)
        self.assertEqual(value["percent_machine"], 25)
        self.assertEqual(value["seconds_per_verified_frame"], .4)
        self.assertIsNone(cpu_metrics(0, 0, 1, 1, 0)["seconds_per_verified_frame"])
        for args in ((0, 0, 0, 1, 0), (-1, 0, 1, 1, 0), (0, 0, 1, 0, 0)):
            with self.assertRaises(ValueError):
                cpu_metrics(*args)

    def test_latency_includes_admission_and_throughput_uses_verified(self):
        data = fixture()
        metrics, complete = derive_metrics(*data, 3, 8)
        self.assertTrue(complete)
        self.assertEqual(metrics["frame_latency_ns"]["p99"], 900)
        self.assertEqual(metrics["effective_fps"], 1.5)
        self.assertIsNone(metrics["gpu_device_time_ns"])
        self.assertIsNone(metrics["memory"]["gpu_allocated_estimate_bytes"])
        data[0]["verified"] = 2
        metrics, complete = derive_metrics(*data, 3, 8)
        self.assertFalse(complete)
        self.assertIsNone(metrics["effective_fps"])

    def test_loss_missing_reordered_and_invalid_counters_never_complete(self):
        for change in ("lost", "missing", "reordered", "backward", "missing_cpu", "backward_cpu",
                       "pool_peak", "failed_exit", "timeout", "summary", "telemetry_off", "rss_units"):
            with self.subTest(change=change):
                child, frames, resources, outer = copy.deepcopy(fixture())
                if change == "lost": child["resource_lost_samples"] = 1
                if change == "missing": frames.pop()
                if change == "reordered": frames.reverse()
                if change == "backward": frames[0][2] = 1000
                if change == "missing_cpu": resources[0][1] = None
                if change == "backward_cpu": resources[1][1] = 0
                if change == "pool_peak": child["encoder_pool_peak"] = 9
                if change == "failed_exit": outer["exit_code"] = 6
                if change == "timeout": outer["timed_out"] = True
                if change == "summary": child["latency_summary"][4] = 1
                if change == "telemetry_off": child["telemetry_enabled"] = False
                if change == "rss_units": outer["rss_peak_bytes"] = 4
                metrics, complete = derive_metrics(child, frames, resources, outer, 3, 8)
                self.assertFalse(complete)
                self.assertIsNone(metrics["effective_fps"])

    def test_missing_optional_memory_is_null_and_missing_required_memory_invalidates(self):
        child, frames, resources, outer = fixture()
        resources[0][5] = None
        metrics, _ = derive_metrics(child, frames, resources, outer, 3, 8)
        self.assertIsNone(metrics["memory"]["physical_footprint_peak_bytes"])
        outer["rss_peak_bytes"] = None
        self.assertEqual(derive_metrics(child, frames, resources, outer, 3, 8), (None, False))

    @unittest.skipUnless(importlib.util.find_spec('jsonschema') and
                         ((ROOT / 'contracts/schemas/performance-report-v1.schema.json').is_file() or
                          (ROOT / 'motion_framework_sas/schemas/performance-report-v1.schema.json').is_file()),
                         'optional local report schema and jsonschema required')
    def test_metrics_schema(self):
        import jsonschema
        schema_path = ROOT / 'contracts/schemas/performance-report-v1.schema.json'
        if not schema_path.is_file():
            schema_path = ROOT / 'motion_framework_sas/schemas/performance-report-v1.schema.json'
        schema = json.loads(schema_path.read_text())
        metrics, _ = derive_metrics(*fixture(), 3, 8)
        jsonschema.Draft202012Validator(schema['properties']['metrics']).validate(metrics)
        metrics['gpu_device_time_ns'] = -1
        with self.assertRaises(jsonschema.ValidationError):
            jsonschema.Draft202012Validator(schema['properties']['metrics']).validate(metrics)


@unittest.skipUnless(BINARY, 'native measurement executable not supplied')
class NativeMeasurementTests(unittest.TestCase):
    def test_real_export_on_off_and_bounded_resource_overflow(self):
        for mode, capacity, expected_complete in (("on", 65536, True), ("off", 65536, False), ("on", 1, False)):
            with self.subTest(mode=mode, capacity=capacity):
                with tempfile.TemporaryDirectory(prefix="instantee-metrics-") as root:
                    outer = wait_for_export([BINARY, root, '4', mode, str(capacity)], timeout=45)
                    self.assertEqual(outer['exit_code'], 0)
                    child = json.loads((Path(root) / 'child.json').read_text())
                    frames = json.loads((Path(root) / 'frames.json').read_text())
                    resources = json.loads((Path(root) / 'resources.json').read_text())
                    metrics, complete = derive_metrics(child, frames, resources, outer, 4, 8)
                    self.assertEqual(complete, expected_complete)
                    self.assertTrue(child['published'])
                    self.assertEqual(child['verified'], 4)
                    self.assertGreater(outer['rss_peak_bytes'], 1024 * 1024)
                    self.assertEqual((Path(root) / 'output.mp4').read_bytes()[4:8], b'ftyp')
                    if capacity == 1:
                        self.assertGreater(metrics['lost_samples'], 0)
                        self.assertEqual(len(resources), 1)

    def test_harness_forwards_interrupt_and_reaps_child(self):
        code = ("import json,os,signal,sys,threading; from measure import wait_for_export; "
                "threading.Timer(.1,lambda: os.kill(os.getpid(),signal.SIGINT)).start(); "
                "result=wait_for_export([sys.executable,'-c','import time;time.sleep(30)'],timeout=5,grace=1); "
                "print(json.dumps(result))")
        result = subprocess.run([sys.executable, '-c', code], cwd=Path(__file__).parent,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0)
        outer = json.loads(result.stdout)
        self.assertTrue(outer['cancelled'])
        self.assertFalse(outer['timed_out'])
        self.assertNotEqual(outer['exit_code'], 0)

    def test_invalid_input_and_timeout_cleanup(self):
        with tempfile.TemporaryDirectory(prefix="instantee-metrics-invalid-") as root:
            for count in ('0', '-1', '9001', '99999999999999999999999999'):
                result = subprocess.run([BINARY, root, count, 'on'], capture_output=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
            self.assertEqual(list(Path(root).iterdir()), [])
        with tempfile.TemporaryDirectory(prefix="instantee-metrics-stop-") as root:
            outer = wait_for_export([BINARY, root, '9000', 'on'], timeout=.15, grace=5)
            self.assertTrue(outer['timed_out'])
            path = Path(root) / 'child.json'
            if path.exists():
                partial = json.loads(path.read_text())
                self.assertTrue(partial['partial'])
                self.assertFalse(partial['published'])
                self.assertLessEqual(partial['accepted'], partial['admitted'])
                self.assertLessEqual(partial['admitted'], partial['scheduled'])
            self.assertFalse((Path(root) / 'output.mp4').exists())
        outer = wait_for_export([sys.executable, '-c', 'import time; time.sleep(60)'], timeout=.03, grace=.03)
        self.assertTrue(outer['timed_out'])
        self.assertNotEqual(outer['exit_code'], 0)


if __name__ == '__main__':
    unittest.main()
