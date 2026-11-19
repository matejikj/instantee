"""Performance report v1: structural schema + semantic certification harness.

Design fixtures (`evidence_kind=schema_fixture`) validate as unmeasured templates.
Measured reports must satisfy BENCHMARK_PROTOCOL §7 semantic invariants before they
may enter a certification series. A single run report never self-certifies.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
CONTRACTS_SCHEMA = REPO_ROOT / "contracts" / "schemas" / "performance-report-v1.schema.json"
DESIGN_PACK_SCHEMA = (
    REPO_ROOT / "motion_framework_sas" / "schemas" / "performance-report-v1.schema.json"
)

REQUIRED_TOP_LEVEL = (
    "schema_version",
    "protocol_version",
    "run_id",
    "evidence_kind",
    "result",
    "certified",
    "machine_profile",
    "build_profile",
    "workload",
    "runtime_config",
    "cache_mode",
    "frames",
    "metrics",
    "output",
    "child_exit_code",
    "artifacts",
)

FRAME_COUNTERS = (
    "scheduled",
    "admitted",
    "accepted",
    "verified",
    "failed",
    "cancelled",
)

MANDATORY_METRIC_KEYS = (
    "export_wall_ms",
    "effective_fps",
    "frame_latency_ns",
    "cpu",
    "memory",
    "in_flight_peak",
    "encoder_pool_peak",
    "queue_peak",
    "lost_samples",
    "gpu_device_time_ns",
    "finalize_ms",
    "phase_times_ms",
)

REL_TOL = 1e-6
ABS_TOL = 1e-9


class PerformanceReportError(ValueError):
    """Raised when a report fails structural or semantic validation."""


def default_schema_path() -> Path:
    if CONTRACTS_SCHEMA.is_file():
        return CONTRACTS_SCHEMA
    if DESIGN_PACK_SCHEMA.is_file():
        return DESIGN_PACK_SCHEMA
    raise FileNotFoundError(
        "performance-report-v1.schema.json not found under contracts/schemas "
        "or motion_framework_sas/schemas"
    )


def load_json(path: Path | str) -> dict[str, Any]:
    with Path(path).open(encoding="utf-8") as handle:
        payload = json.load(handle)
    if not isinstance(payload, dict):
        raise PerformanceReportError("performance report root must be an object")
    return payload


def _approx_equal(left: float, right: float) -> bool:
    scale = max(abs(left), abs(right), 1.0)
    return abs(left - right) <= max(ABS_TOL, REL_TOL * scale)


def _is_int(value: Any) -> bool:
    return type(value) is int


def _as_number(value: Any) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    number = float(value)
    if not math.isfinite(number):
        return None
    return number


def _require_mapping(value: Any, label: str, issues: list[str]) -> dict[str, Any] | None:
    if not isinstance(value, dict):
        issues.append(f"{label} must be an object")
        return None
    return value


def structural_issues_fallback(report: dict[str, Any]) -> list[str]:
    """Shape checks used when Python jsonschema is unavailable."""
    issues: list[str] = []
    for key in REQUIRED_TOP_LEVEL:
        if key not in report:
            issues.append(f"missing required field: {key}")
    if report.get("schema_version") != "1.0.0":
        issues.append("schema_version must be 1.0.0")
    if report.get("protocol_version") != "1.0.0":
        issues.append("protocol_version must be 1.0.0")
    if report.get("evidence_kind") not in {"measured", "schema_fixture"}:
        issues.append("evidence_kind must be measured or schema_fixture")
    if report.get("result") not in {"not_run", "success", "failed", "cancelled"}:
        issues.append("result must be not_run|success|failed|cancelled")
    if report.get("certified") is not False:
        issues.append("certified must be false on every run report")
    if report.get("cache_mode") not in {"cold", "warm"}:
        issues.append("cache_mode must be cold or warm")

    frames = report.get("frames")
    if not isinstance(frames, dict):
        issues.append("frames must be an object")
    else:
        for key in FRAME_COUNTERS:
            if not _is_int(frames.get(key)) or frames[key] < 0:
                issues.append(f"frames.{key} must be a non-negative integer")

    kind = report.get("evidence_kind")
    if kind == "schema_fixture":
        if report.get("result") != "not_run":
            issues.append("schema_fixture requires result=not_run")
        if report.get("metrics") is not None:
            issues.append("schema_fixture requires metrics=null")
        if report.get("machine_profile") is not None or report.get("build_profile") is not None:
            issues.append("schema_fixture requires null machine/build profiles")
        if report.get("child_exit_code") is not None:
            issues.append("schema_fixture requires child_exit_code=null")
        output = report.get("output")
        if isinstance(output, dict):
            if output.get("published") is not False:
                issues.append("schema_fixture requires published=false")
            if output.get("achieved_durability") != "not_requested":
                issues.append("schema_fixture requires achieved_durability=not_requested")
            if output.get("video_uri") is not None:
                issues.append("schema_fixture requires video_uri=null")
        if report.get("artifacts") not in ([],):
            if not isinstance(report.get("artifacts"), list) or report.get("artifacts"):
                issues.append("schema_fixture requires empty artifacts")
        if isinstance(frames, dict) and any(frames.get(key) != 0 for key in FRAME_COUNTERS):
            issues.append("schema_fixture requires zero frame counters")

    if kind == "measured":
        if report.get("result") == "not_run":
            issues.append("measured reports cannot use result=not_run")
        if not isinstance(report.get("machine_profile"), dict):
            issues.append("measured reports require machine_profile object")
        if not isinstance(report.get("build_profile"), dict):
            issues.append("measured reports require build_profile object")
        if report.get("result") == "success":
            metrics = report.get("metrics")
            if not isinstance(metrics, dict):
                issues.append("success requires metrics object")
            else:
                if metrics.get("effective_fps") is None:
                    issues.append("success requires non-null effective_fps")
                if not isinstance(metrics.get("frame_latency_ns"), dict):
                    issues.append("success requires frame_latency_ns object")
                if metrics.get("lost_samples") != 0:
                    issues.append("success requires lost_samples=0")
            if report.get("child_exit_code") != 0:
                issues.append("success requires child_exit_code=0")
            output = report.get("output")
            if not isinstance(output, dict) or output.get("published") is not True:
                issues.append("success requires published=true")
            if isinstance(output, dict) and output.get("achieved_durability") not in {
                "atomic_visibility",
                "durable_local",
            }:
                issues.append("success requires durable publication mode")
            if isinstance(output, dict) and not isinstance(output.get("video_uri"), str):
                issues.append("success requires video_uri string")
            artifacts = report.get("artifacts")
            if not isinstance(artifacts, list) or len(artifacts) < 2:
                issues.append("success requires at least two artifacts")
    return issues


def validate_structure(
    report: dict[str, Any],
    *,
    schema_path: Path | None = None,
    require_jsonschema: bool = False,
) -> dict[str, Any]:
    """Validate structure via jsonschema when available, else fallback checks.

    Returns metadata describing which structural backend ran.
    """
    path = (schema_path or default_schema_path()).resolve()
    try:
        schema_display = str(path.relative_to(REPO_ROOT))
    except ValueError:
        schema_display = str(path)
    meta: dict[str, Any] = {"schema_path": schema_display, "backend": None}
    try:
        import jsonschema
    except ImportError:
        jsonschema = None  # type: ignore[assignment]

    if jsonschema is not None:
        with path.open(encoding="utf-8") as handle:
            schema = json.load(handle)
        validator = jsonschema.Draft202012Validator(schema)
        errors = sorted(validator.iter_errors(report), key=lambda err: list(err.path))
        if errors:
            first = errors[0]
            where = ".".join(str(part) for part in first.path) or "<root>"
            raise PerformanceReportError(
                f"schema validation failed at {where}: {first.message}"
            )
        meta["backend"] = "jsonschema"
        return meta

    if require_jsonschema:
        raise PerformanceReportError(
            "jsonschema is required for structural performance-report validation"
        )

    issues = structural_issues_fallback(report)
    if issues:
        raise PerformanceReportError("; ".join(issues))
    meta["backend"] = "fallback"
    return meta


def collect_semantic_issues(
    report: dict[str, Any],
    *,
    expected_frames: int | None = None,
) -> list[str]:
    """Return BENCHMARK_PROTOCOL §7 semantic violations (empty when consistent)."""
    issues: list[str] = []
    kind = report.get("evidence_kind")
    if kind not in {"measured", "schema_fixture"}:
        issues.append("evidence_kind must be measured or schema_fixture")
        return issues

    if report.get("certified") is not False:
        issues.append("run reports must set certified=false; series certification is separate")

    if kind == "schema_fixture":
        if report.get("result") != "not_run":
            issues.append("schema_fixture must use result=not_run")
        if report.get("metrics") is not None:
            issues.append("schema_fixture must not carry measured metrics")
        return issues

    # measured --------------------------------------------------------------
    result = report.get("result")
    if result not in {"success", "failed", "cancelled"}:
        issues.append("measured result must be success|failed|cancelled")

    workload = _require_mapping(report.get("workload"), "workload", issues)
    frames = _require_mapping(report.get("frames"), "frames", issues)
    runtime = _require_mapping(report.get("runtime_config"), "runtime_config", issues)
    output = _require_mapping(report.get("output"), "output", issues)
    if frames is None:
        return issues

    for key in FRAME_COUNTERS:
        if not _is_int(frames.get(key)) or frames[key] < 0:
            issues.append(f"frames.{key} must be a non-negative integer")

    expected = expected_frames
    if expected is None and workload is not None:
        expected = workload.get("duration_frames")
    if expected is not None and (not _is_int(expected) or expected < 1):
        issues.append("expected_frames must be a positive integer")
        expected = None

    metrics = report.get("metrics")
    if result == "success":
        if expected is None:
            issues.append("success requires expected_frames / workload.duration_frames")
        if not isinstance(metrics, dict):
            issues.append("success requires metrics object with mandatory fields")
            return issues
        for key in MANDATORY_METRIC_KEYS:
            if key not in metrics:
                issues.append(f"missing mandatory metric: {key}")

        if expected is not None:
            for key in ("scheduled", "admitted", "accepted", "verified"):
                if frames.get(key) != expected:
                    issues.append(
                        f"frames.{key} ({frames.get(key)}) != expected_frames ({expected})"
                    )
            if frames.get("failed") != 0 or frames.get("cancelled") != 0:
                issues.append("success requires failed=cancelled=0")

        if metrics.get("lost_samples") != 0:
            issues.append("success requires lost_samples=0")

        wall_ms = _as_number(metrics.get("export_wall_ms"))
        if wall_ms is None or wall_ms <= 0.0:
            issues.append("export_wall_ms must be a positive number")
            wall_ms = None

        fps = metrics.get("effective_fps")
        fps_n = _as_number(fps)
        if fps is None:
            issues.append("success requires measured effective_fps (null means unavailable)")
        elif fps_n is None:
            issues.append("effective_fps must be a finite number")
        elif wall_ms is not None and _is_int(frames.get("verified")):
            expected_fps = frames["verified"] / (wall_ms / 1000.0)
            if not _approx_equal(fps_n, expected_fps):
                issues.append(
                    "effective_fps inconsistent with verified/(export_wall_ms/1000) "
                    f"({fps_n} vs {expected_fps})"
                )

        latency = metrics.get("frame_latency_ns")
        if not isinstance(latency, dict):
            issues.append("success requires frame_latency_ns object")
        else:
            for key in ("sample_count", "min", "p50", "p95", "p99", "max"):
                if not _is_int(latency.get(key)) or latency[key] < 0:
                    issues.append(f"frame_latency_ns.{key} must be a non-negative integer")
            if latency.get("method") != "exact_nearest_rank":
                issues.append("frame_latency_ns.method must be exact_nearest_rank")
            if latency.get("unit") != "ns":
                issues.append("frame_latency_ns.unit must be ns")
            if latency.get("interval") != "request_before_admission_to_sink_accepted":
                issues.append("frame_latency_ns.interval must match admission→sink contract")
            sample_count = latency.get("sample_count")
            accepted = frames.get("accepted")
            if _is_int(sample_count) and _is_int(accepted) and sample_count != accepted:
                issues.append(
                    f"latency sample_count ({sample_count}) != frames.accepted ({accepted})"
                )
            ordered = [latency.get(k) for k in ("min", "p50", "p95", "p99", "max")]
            if all(_is_int(v) for v in ordered):
                if not (ordered[0] <= ordered[1] <= ordered[2] <= ordered[3] <= ordered[4]):
                    issues.append("latency quantiles must satisfy min≤p50≤p95≤p99≤max")

        cpu = metrics.get("cpu")
        if not isinstance(cpu, dict):
            issues.append("success requires cpu metrics object")
        else:
            if cpu.get("scope") != "renderer_process_all_threads":
                issues.append("cpu.scope must be renderer_process_all_threads")
            if not isinstance(cpu.get("source"), str) or not cpu["source"]:
                issues.append("cpu.source is mandatory")
            for key in ("user_ns", "system_ns"):
                if not _is_int(cpu.get(key)) or cpu[key] < 0:
                    issues.append(f"cpu.{key} must be a non-negative integer")
            for key in ("core_equivalents", "percent_one_core", "percent_machine"):
                if _as_number(cpu.get(key)) is None or cpu[key] < 0:
                    issues.append(f"cpu.{key} must be a non-negative number")
            user = cpu.get("user_ns")
            system = cpu.get("system_ns")
            cores = _as_number(cpu.get("core_equivalents"))
            one = _as_number(cpu.get("percent_one_core"))
            machine = _as_number(cpu.get("percent_machine"))
            wall_ms_cpu = wall_ms
            logical = None
            machine_profile = report.get("machine_profile")
            if isinstance(machine_profile, dict):
                logical = machine_profile.get("logical_cpu_count")
            if (
                _is_int(user)
                and _is_int(system)
                and wall_ms_cpu is not None
                and wall_ms_cpu > 0
                and cores is not None
            ):
                expected_cores = (user + system) / (wall_ms_cpu * 1e6)
                if not _approx_equal(cores, expected_cores):
                    issues.append("cpu.core_equivalents inconsistent with user/system/wall")
            if cores is not None and one is not None and not _approx_equal(one, 100.0 * cores):
                issues.append("cpu.percent_one_core inconsistent with core_equivalents")
            if (
                cores is not None
                and machine is not None
                and _is_int(logical)
                and logical > 0
                and not _approx_equal(machine, 100.0 * cores / logical)
            ):
                issues.append("cpu.percent_machine inconsistent with core_equivalents/logical_cpus")
            spvf = cpu.get("seconds_per_verified_frame")
            verified = frames.get("verified")
            if spvf is None:
                if _is_int(verified) and verified > 0:
                    issues.append("seconds_per_verified_frame required when verified>0")
            else:
                spvf_n = _as_number(spvf)
                if spvf_n is None or spvf_n < 0:
                    issues.append("seconds_per_verified_frame must be a non-negative number")
                elif (
                    _is_int(user)
                    and _is_int(system)
                    and _is_int(verified)
                    and verified > 0
                    and not _approx_equal(spvf_n, (user + system) / 1e9 / verified)
                ):
                    issues.append("seconds_per_verified_frame inconsistent with CPU totals")

        memory = metrics.get("memory")
        if not isinstance(memory, dict):
            issues.append("success requires memory metrics object")
        else:
            if not _is_int(memory.get("rss_peak_bytes")) or memory["rss_peak_bytes"] < 1:
                issues.append("memory.rss_peak_bytes must be a positive integer")
            if memory.get("unit") != "bytes":
                issues.append("memory.unit must be bytes")
            if not isinstance(memory.get("rss_source"), str) or not memory["rss_source"]:
                issues.append("memory.rss_source is mandatory")

        if runtime is not None:
            for peak_key, cap_key in (
                ("queue_peak", "queue_capacity"),
                ("encoder_pool_peak", "encoder_pool_e"),
                ("in_flight_peak", "frame_credits_k"),
            ):
                peak = metrics.get(peak_key)
                capacity = runtime.get(cap_key)
                if not _is_int(peak) or peak < 0:
                    issues.append(f"{peak_key} must be a non-negative integer")
                    continue
                if _is_int(capacity) and capacity >= 1 and peak > capacity:
                    issues.append(f"{peak_key} ({peak}) exceeds {cap_key} ({capacity})")

        if output is not None:
            if output.get("published") is not True:
                issues.append("success requires published=true")
            if output.get("achieved_durability") not in {"atomic_visibility", "durable_local"}:
                issues.append("success requires achieved durability mode")
            if not isinstance(output.get("video_uri"), str):
                issues.append("success requires video_uri")

        if report.get("child_exit_code") != 0:
            issues.append("success requires child_exit_code=0")

        artifacts = report.get("artifacts")
        if not isinstance(artifacts, list) or len(artifacts) < 2:
            issues.append("success requires artifact digests for sample evidence")
    elif isinstance(metrics, dict):
        # Partial measured metrics still cannot lie about quantile order / peaks.
        latency = metrics.get("frame_latency_ns")
        if isinstance(latency, dict):
            ordered = [latency.get(k) for k in ("min", "p50", "p95", "p99", "max")]
            if all(_is_int(v) for v in ordered):
                if not (ordered[0] <= ordered[1] <= ordered[2] <= ordered[3] <= ordered[4]):
                    issues.append("latency quantiles must satisfy min≤p50≤p95≤p99≤max")
        if runtime is not None:
            for peak_key, cap_key in (
                ("queue_peak", "queue_capacity"),
                ("encoder_pool_peak", "encoder_pool_e"),
                ("in_flight_peak", "frame_credits_k"),
            ):
                peak = metrics.get(peak_key)
                capacity = runtime.get(cap_key)
                if _is_int(peak) and _is_int(capacity) and capacity >= 1 and peak > capacity:
                    issues.append(f"{peak_key} ({peak}) exceeds {cap_key} ({capacity})")

    return issues


def validate_semantics(
    report: dict[str, Any],
    *,
    expected_frames: int | None = None,
) -> None:
    issues = collect_semantic_issues(report, expected_frames=expected_frames)
    if issues:
        raise PerformanceReportError("; ".join(issues))


def validate_performance_report(
    report: dict[str, Any],
    *,
    schema_path: Path | None = None,
    expected_frames: int | None = None,
    require_jsonschema: bool = False,
) -> dict[str, Any]:
    """Structural + semantic validation. Returns structural backend metadata."""
    meta = validate_structure(
        report,
        schema_path=schema_path,
        require_jsonschema=require_jsonschema,
    )
    validate_semantics(report, expected_frames=expected_frames)
    return meta


def is_certification_eligible(
    report: dict[str, Any],
    *,
    expected_frames: int | None = None,
) -> bool:
    """True when a measured success report is semantically consistent (still uncertified)."""
    if report.get("evidence_kind") != "measured":
        return False
    if report.get("result") != "success":
        return False
    if report.get("certified") is not False:
        return False
    return not collect_semantic_issues(report, expected_frames=expected_frames)


def validate_report_file(
    path: Path | str,
    *,
    schema_path: Path | None = None,
    expected_frames: int | None = None,
    require_jsonschema: bool = False,
) -> dict[str, Any]:
    report = load_json(path)
    validate_performance_report(
        report,
        schema_path=schema_path,
        expected_frames=expected_frames,
        require_jsonschema=require_jsonschema,
    )
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path, help="performance report JSON path")
    parser.add_argument("--schema", type=Path, default=None)
    parser.add_argument("--expected-frames", type=int, default=None)
    parser.add_argument(
        "--require-jsonschema",
        action="store_true",
        help="fail when the jsonschema package is unavailable",
    )
    args = parser.parse_args(argv)
    report = load_json(args.report)
    meta = validate_performance_report(
        report,
        schema_path=args.schema,
        expected_frames=args.expected_frames,
        require_jsonschema=args.require_jsonschema,
    )
    eligible = is_certification_eligible(report, expected_frames=args.expected_frames)
    print(
        json.dumps(
            {
                "ok": True,
                "evidence_kind": report.get("evidence_kind"),
                "result": report.get("result"),
                "certification_eligible": eligible,
                "certified": report.get("certified"),
                "structure": meta,
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, PerformanceReportError, json.JSONDecodeError) as exc:
        print(json.dumps({"ok": False, "error": str(exc)}), file=sys.stderr)
        raise SystemExit(2) from exc
