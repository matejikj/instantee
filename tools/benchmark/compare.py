#!/usr/bin/env python3
"""Retain paired telemetry-on/off trials without claiming certified performance."""
from pathlib import Path
import statistics
import sys

from measure import atomic_json, digest, measure, read_json, MeasurementArguments


def compare(binary, directory, profile_path, frames=30, repeats=3, schema=None, timeout=120, grace=5):
    if not 3 <= repeats <= 10:
        raise ValueError("comparison requires 3 to 10 pairs")
    profile = read_json(profile_path, 65536)
    directory = Path(directory)
    directory.mkdir()
    pairs = []
    for index in range(repeats):
        trials = {}
        for enabled in ((False, True) if index % 2 else (True, False)):
            name = "on" if enabled else "off"
            report, outer = measure(binary, directory / f"pair-{index:02d}-{name}",
                                    frames, enabled, profile, timeout=timeout, grace=grace)
            if schema:
                from jsonschema import Draft202012Validator
                if not Draft202012Validator(read_json(schema)).is_valid(report):
                    raise ValueError("performance report schema validation failed")
            if (report["child_exit_code"] != 0 or report["frames"]["verified"] != frames or
                    not report["output"]["published"] or
                    report["runtime_config"]["validation_mode"] != "disabled" or
                    (enabled and report["result"] != "success")):
                raise ValueError("incomplete or instrumented overhead trial; retained for diagnosis")
            trials[name] = {"wall_ns": outer["wall_ns"],
                            "cpu_ns": outer["user_ns"] + outer["system_ns"],
                            "rss_peak_bytes": outer["rss_peak_bytes"]}
        wall_off, wall_on = trials["off"]["wall_ns"], trials["on"]["wall_ns"]
        pairs.append({"pair": index, **trials, "wall_difference_ns": wall_on - wall_off,
                      "wall_ratio_on_over_off": wall_on / wall_off,
                      "cpu_difference_ns": trials["on"]["cpu_ns"] - trials["off"]["cpu_ns"],
                      "rss_difference_bytes": trials["on"]["rss_peak_bytes"] - trials["off"]["rss_peak_bytes"]})
    result = {"evidence_kind": "telemetry_overhead_experiment", "certified": False,
              "frames_per_trial": frames, "pairs": pairs, "profile_sha256": digest(profile_path),
              "boundary": "parent_monotonic_before_spawn_to_after_wait4",
              "cpu_scope": "renderer_process_all_threads", "time_unit": "ns", "memory_unit": "bytes",
              "cache_policy": "new process and no persistent application cache; OS and driver caches uncontrolled",
              "median_wall_ratio_on_over_off": statistics.median(p["wall_ratio_on_over_off"] for p in pairs),
              "notes": ["All trials retained; alternating pair order; no overhead subtraction",
                        "Short measurement validation workload; no throughput, p99 or memory certification"]}
    atomic_json(directory / "overhead.json", result)
    return result


def main():
    parser = MeasurementArguments(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=30)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--grace", type=float, default=5)
    parser.add_argument("--schema", type=Path)
    args = parser.parse_args()
    compare(args.binary, args.directory, args.profile, args.frames, args.repeats, args.schema, args.timeout, args.grace)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError):
        print('{"code":"comparison_failed","phase":"harness","published":null,"durability":"unknown"}', file=sys.stderr)
        raise SystemExit(6)
