#!/usr/bin/env python3
"""Measure the native test-pattern export from immediately before spawn to wait4."""

import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


class MeasurementArguments(argparse.ArgumentParser):
    def error(self, _message):
        self.exit(2, '{"code":"invalid_measurement_arguments","phase":"arguments",'
                     '"published":false,"durability":"not_published"}\n')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_json(path, limit=16 * 1024 * 1024):
    path = Path(path)
    if path.stat().st_size > limit:
        raise ValueError("measurement JSON exceeds its bounded input limit")
    return json.loads(path.read_text())


def atomic_json(path, value):
    path = Path(path)
    # One report is an independent artifact, never a two-file transaction.
    fd, temporary = tempfile.mkstemp(prefix=".report-", dir=path.parent)
    try:
        with os.fdopen(fd, "w") as stream:
            json.dump(value, stream, indent=2, allow_nan=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def cpu_metrics(user_ns, system_ns, wall_ns, logical_cpus, verified):
    if (not all(integer(v) for v in (user_ns, system_ns, wall_ns, logical_cpus, verified)) or
            wall_ns == 0 or logical_cpus == 0):
        raise ValueError("invalid CPU measurement")
    total = user_ns + system_ns
    cores = total / wall_ns
    return {"user_ns": user_ns, "system_ns": system_ns, "source": "Darwin wait4 rusage timeval",
            "scope": "renderer_process_all_threads", "core_equivalents": cores,
            "percent_one_core": 100 * cores, "percent_machine": 100 * cores / logical_cpus,
            "seconds_per_verified_frame": total / 1e9 / verified if verified else None}


def nearest_rank(values):
    if not values:
        return None
    ordered = sorted(values)
    count = len(ordered)
    return {"sample_count": count, "min": ordered[0],
            **{name: ordered[(count * percent + 99) // 100 - 1]
               for name, percent in (("p50", 50), ("p95", 95), ("p99", 99))},
            "max": ordered[-1], "method": "exact_nearest_rank",
            "interval": "request_before_admission_to_sink_accepted", "unit": "ns"}


def integer(value):
    return type(value) is int and 0 <= value <= 2**64 - 1


def derive_metrics(child, frames, resources, outer, expected, logical_cpus):
    """Reject incomplete observations; null never means a measured zero."""
    counter_keys = ("frame_lost_samples", "resource_lost_samples", "scheduled", "admitted",
                    "accepted", "verified", "in_flight_peak", "queue_peak", "encoder_pool_peak")
    if (not all(integer(child[k]) for k in counter_keys) or
            type(child["telemetry_enabled"]) is not bool or type(child["published"]) is not bool or
            not isinstance(child["finalize_ms"], (int, float)) or
            not math.isfinite(child["finalize_ms"]) or child["finalize_ms"] < 0):
        raise ValueError("invalid child measurement counters")
    lost = child["frame_lost_samples"] + child["resource_lost_samples"]
    valid_frames = len(frames) == expected
    previous_request = previous_accepted = 0
    latency = []
    for index, row in enumerate(frames):
        if (not isinstance(row, list) or len(row) != 9 or not all(integer(v) for v in row) or row[0] != index or
                row[1:] != sorted(row[1:]) or row[1] < previous_request or row[8] < previous_accepted):
            valid_frames = False
            continue
        previous_request, previous_accepted = row[1], row[8]
        latency.append(row[8] - row[1])
    summary = nearest_rank(latency) if valid_frames and not lost else None
    native_summary = ([summary[k] for k in ("sample_count", "min", "p50", "p95", "p99", "max")]
                      if summary else None)
    summary_matches = native_summary == child["latency_summary"]
    resource_valid = len(resources) >= 2
    previous = None
    footprint = []
    for row in resources:
        if not isinstance(row, list) or len(row) != 6 or not all(integer(v) for v in row[:5]):
            resource_valid = False
            continue
        if row[3] == 0 or row[4] < row[3] or (row[5] is not None and not integer(row[5])):
            resource_valid = False
        if previous and any(row[i] < previous[i] for i in (0, 1, 2, 4)):
            resource_valid = False
        previous = row
        if row[5] is not None:
            footprint.append(row[5])
    if resource_valid:
        resource_valid = (integer(outer["rss_peak_bytes"]) and
                          all(row[4] <= outer["rss_peak_bytes"] for row in resources))
    complete = (child["telemetry_enabled"] and valid_frames and summary_matches and
                resource_valid and not lost and outer["exit_code"] == 0 and not outer["timed_out"] and not outer.get("cancelled", False) and
                child["published"] and all(child[k] == expected for k in
                    ("scheduled", "admitted", "accepted", "verified")) and
                0 < child["in_flight_peak"] <= 2 and 0 < child["queue_peak"] <= 1 and
                child["encoder_pool_peak"] == 8)
    if outer["wall_ns"] <= 0 or not integer(outer["rss_peak_bytes"]) or outer["rss_peak_bytes"] == 0:
        return None, False
    cpu = cpu_metrics(outer["user_ns"], outer["system_ns"], outer["wall_ns"],
                      logical_cpus, child["verified"])
    metrics = {"export_wall_ms": outer["wall_ns"] / 1e6,
               "effective_fps": child["verified"] * 1e9 / outer["wall_ns"] if complete else None,
               "frame_latency_ns": summary if summary_matches else None, "cpu": cpu,
               "memory": {"rss_peak_bytes": outer["rss_peak_bytes"],
                          "rss_source": "Darwin wait4 ru_maxrss; corroborated against MACH_TASK_BASIC_INFO",
                          "unit": "bytes", "physical_footprint_peak_bytes":
                          max(footprint) if len(footprint) == len(resources) and footprint else None,
                          "gpu_allocated_estimate_bytes": None},
               "in_flight_peak": child["in_flight_peak"], "encoder_pool_peak": child["encoder_pool_peak"],
               "queue_peak": child["queue_peak"], "lost_samples": lost,
               "gpu_device_time_ns": None, "finalize_ms": child["finalize_ms"], "phase_times_ms": {}}
    return metrics, bool(complete)


def wait_for_export(command, timeout=120, grace=5):
    if sys.platform != "darwin":
        raise RuntimeError("Darwin byte accounting must be verified on the native host")
    if not all(math.isfinite(v) and v > 0 for v in (timeout, grace)):
        raise ValueError("timeouts must be positive and finite")
    # Neither Popen.poll nor wait is used before wait4: they would reap the child
    # and lose its per-process accounting. Child output never becomes evidence.
    requested = 0

    def remember(signum, _frame):
        nonlocal requested
        requested = signum

    previous = {sig: signal.signal(sig, remember) for sig in (signal.SIGINT, signal.SIGTERM)}
    start = time.monotonic_ns()
    try:
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   start_new_session=True)
    except BaseException:
        for sig, handler in previous.items():
            signal.signal(sig, handler)
        raise
    deadline = start + int(timeout * 1e9)
    timed_out = False
    killed = False
    forwarded = False
    try:
        while True:
            pid, status, usage = os.wait4(process.pid, os.WNOHANG)
            if pid:
                end = time.monotonic_ns()
                process.returncode = os.waitstatus_to_exitcode(status)
                return {"wall_ns": end - start, "exit_code": process.returncode,
                        "user_ns": int(round(usage.ru_utime * 1e6)) * 1000,
                        "system_ns": int(round(usage.ru_stime * 1e6)) * 1000,
                        "rss_peak_bytes": int(usage.ru_maxrss) if usage.ru_maxrss > 0 else None,
                        "timed_out": timed_out, "cancelled": bool(requested),
                        "timeout_seconds": timeout, "grace_seconds": grace}
            now = time.monotonic_ns()
            if not forwarded and (requested or now >= deadline):
                timed_out = not requested
                try:
                    os.killpg(process.pid, requested or signal.SIGTERM)
                except ProcessLookupError:
                    pass
                forwarded = True
                deadline = now + int(grace * 1e9)
            elif forwarded and now >= deadline and not killed:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                killed = True
            time.sleep(0.001)
    finally:
        if process.returncode is None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            _, status = os.waitpid(process.pid, 0)
            process.returncode = os.waitstatus_to_exitcode(status)
        for sig, handler in previous.items():
            signal.signal(sig, handler)


PROFILE_KEYS = {
    "machine_profile": {"model", "os_build", "logical_cpu_count", "gpu_cores", "ram_bytes", "filesystem", "power_mode", "thermal_state_start", "thermal_state_end"},
    "build_profile": {"source_revision", "source_dirty", "source_diff_sha256", "binary_sha256", "toolchain_id", "compiler_flags", "dependency_lock_sha256", "shaders_sha256"},
    "workload": {"id", "scene_sha256", "asset_lock_sha256", "duration_frames", "width", "height", "frame_rate", "seed_decimal", "quality_tier", "encoder_profile"}}


def validate_profile(profile):
    if not isinstance(profile, dict) or set(profile) != set(PROFILE_KEYS):
        raise ValueError("profile must contain only allowlisted sections")
    for section, keys in PROFILE_KEYS.items():
        if not isinstance(profile[section], dict) or set(profile[section]) != keys:
            raise ValueError("profile fields differ from allowlist")
    workload = profile["workload"]
    if (workload["width"] != 1080 or workload["height"] != 1920 or
            workload["frame_rate"] != {"numerator": 30, "denominator": 1}):
        raise ValueError("profile differs from the native test-pattern geometry")


def measure(binary, bundle, expected, telemetry, profile, capacity=65536, timeout=120, grace=5):
    validate_profile(profile)
    if not 1 <= expected <= 9000:
        raise ValueError("frame count must be in [1, 9000]")
    if profile["build_profile"]["binary_sha256"] != digest(binary):
        raise ValueError("binary differs from the pinned build profile")
    bundle = Path(bundle)
    bundle.mkdir()
    output = bundle / "output"
    output.mkdir()
    (bundle / "reports").mkdir()
    outer = wait_for_export([str(Path(binary).resolve()), str(output.resolve()), str(expected),
                             "on" if telemetry else "off", str(capacity)], timeout, grace)
    child_path = output / "child.json"
    child = read_json(child_path) if child_path.exists() else None
    frames = read_json(output / "frames.json") if (output / "frames.json").exists() else []
    resources = read_json(output / "resources.json") if (output / "resources.json").exists() else []
    metrics, complete = (derive_metrics(child, frames, resources, outer, expected,
                                       profile["machine_profile"]["logical_cpu_count"])
                         if child and not child.get("partial", False) else (None, False))
    report = {"schema_version": "1.0.0", "protocol_version": "1.0.0", "run_id": bundle.name,
              "evidence_kind": "measured", "result": "success" if complete else ("cancelled" if outer["cancelled"] else "failed"), "certified": False,
              "machine_profile": copy.deepcopy(profile["machine_profile"]),
              "build_profile": copy.deepcopy(profile["build_profile"]),
              "workload": copy.deepcopy(profile["workload"]),
              "runtime_config": {"workers": 1, "frame_credits_k": 2, "encoder_pool_e": 8,
                                 "queue_capacity": 1, "p99_limit_ns": None, "rss_budget_bytes": 6 * 1024**3,
                                 "cpu_baseline_seconds_per_frame": None, "durability_mode": "atomic_visibility",
                                 "conformance_mode": "full_decode", "validation_mode":
                                 child["validation_mode"] if child else "disabled", "telemetry_period_ms": 100},
              "cache_mode": "cold",
              "frames": {k: child[k] if child else 0 for k in ("scheduled", "admitted", "accepted", "verified")},
              "metrics": metrics, "output": {"published": (output / "output.mp4").exists(),
                     "achieved_durability": "atomic_visibility" if child and child["published"] and not child.get("partial", False) else "unknown",
                     "video_uri": "output/output.mp4" if (output / "output.mp4").exists() else None},
              "child_exit_code": outer["exit_code"], "artifacts": []}
    report["frames"].update(failed=expected if not child and not outer["cancelled"] else 0,
                            cancelled=expected if not child and outer["cancelled"] else 0)
    report["workload"]["duration_frames"] = expected
    for kind, filename in (("latency_samples", "frames.json"), ("resource_samples", "resources.json"),
                           ("child_report", "child.json")):
        path = output / filename
        if path.exists():
            report["artifacts"].append({"kind": kind, "uri": "output/" + filename, "sha256": digest(path)})
    atomic_json(bundle / "reports/report.json", report)
    outer["partial_counts_available"] = child is not None
    # External counters are retained even when telemetry is deliberately off.
    atomic_json(bundle / "reports/outer.json", outer)
    return report, outer


def main():
    parser = MeasurementArguments(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=30)
    parser.add_argument("--telemetry", choices=("on", "off"), default="on")
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--grace", type=float, default=5)
    parser.add_argument("--schema", type=Path)
    args = parser.parse_args()
    profile = read_json(args.profile, 65536)
    report, _ = measure(args.binary, args.bundle, args.frames, args.telemetry == "on", profile, timeout=args.timeout, grace=args.grace)
    if args.schema:
        from jsonschema import Draft202012Validator
        if not Draft202012Validator(read_json(args.schema)).is_valid(report):
            raise ValueError("performance report schema validation failed")
    return 0 if report["result"] == "success" else 6


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError):
        print('{"code":"measurement_failed","phase":"harness","published":null,"durability":"unknown"}', file=sys.stderr)
        raise SystemExit(6)
