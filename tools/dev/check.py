#!/usr/bin/env python3
"""Run the risk-selected local verification set and optionally write checks.json."""

from __future__ import annotations

import argparse
import dataclasses
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
REPORT_SCHEMA_VERSION = 1

NATIVE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".m", ".mm", ".metal"}
FORMAT_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".m", ".mm"}
TIDY_SUFFIXES = {".c", ".cc", ".cpp", ".m", ".mm"}
EXIT_SKIPPED = 3
BUILD_FILES = {"CMakeLists.txt", "CMakePresets.json", "vcpkg.json", "vcpkg-configuration.json"}
CONCURRENCY_PATTERN = re.compile(
    r"std::(?:j?thread|atomic|condition_variable|stop_token|stop_source|counting_semaphore)"
    r"|\bdispatch_\w+|\bpthread_\w+"
)
CONCURRENCY_PREFIXES = ("engine/src/runtime/", "engine/tests/unit/runtime/")
TIMING_PREFIXES = (
    "engine/native/export-apple/",
    "engine/src/export-core/",
    "engine/src/core-model/",
    "engine/tests/unit/export-apple/",
)
AUTHORING_PREFIXES = ("authoring/", "contracts/")
AUTHORING_FILES = {"package.json", "package-lock.json", "tsconfig.json", "tsconfig.base.json"}
PYTHON_SUITES = ("tools/benchmark", "tools/dev", "tools/machine-profile", "tools/toolchain")
CTEST_SUMMARY = re.compile(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)")


@dataclasses.dataclass(frozen=True)
class Risks:
    native: bool = False
    concurrency: bool = False
    timing: bool = False
    authoring: bool = False
    python_suites: tuple[str, ...] = ()

    def labels(self) -> list[str]:
        names = [
            name for name in ("native", "concurrency", "timing", "authoring")
            if getattr(self, name)
        ]
        return names + [f"python:{suite}" for suite in self.python_suites]


@dataclasses.dataclass(frozen=True)
class Step:
    name: str
    argv: tuple[str, ...]
    requires: str | None = None
    skip_exit_code: int | None = None

    def command(self) -> str:
        return " ".join(self.argv)


def is_build_file(path: str) -> bool:
    name = path.rsplit("/", 1)[-1]
    return name in BUILD_FILES or path.startswith("cmake/") or name.endswith(".cmake")


def infer_risks(paths: list[str], read_text=None) -> Risks:
    """Classify changed repository-relative paths into verification risks."""
    native = concurrency = timing = authoring = False
    suites: set[str] = set()
    for path in paths:
        suffix = Path(path).suffix
        is_native = suffix in NATIVE_SUFFIXES and path.startswith("engine/")
        if is_native or (is_build_file(path) and not path.startswith(AUTHORING_PREFIXES)):
            native = True
        if path.startswith(CONCURRENCY_PREFIXES):
            concurrency = True
        elif is_native and read_text is not None:
            text = read_text(path)
            if text is not None and CONCURRENCY_PATTERN.search(text):
                concurrency = True
        if path.startswith(TIMING_PREFIXES):
            timing = True
        if path.startswith(AUTHORING_PREFIXES) or path in AUTHORING_FILES:
            authoring = True
        for suite in PYTHON_SUITES:
            if path.startswith(suite + "/"):
                suites.add(suite)
    return Risks(native, concurrency, timing, authoring, tuple(sorted(suites)))


def tidy_sources(paths: list[str]) -> list[str]:
    return sorted(p for p in paths if p.startswith("engine/") and Path(p).suffix in TIDY_SUFFIXES)


def plan_steps(
    paths: list[str],
    risks: Risks,
    *,
    gate: bool,
    configured: bool,
    tidy_paths: list[str] | None = None,
) -> list[Step]:
    """Return the ordered checks for the changed paths and risks.

    tidy_paths are the translation units for clang-tidy; None when the
    repository has no .clang-tidy configuration.
    """
    if gate:
        risks = Risks(True, True, True, True, PYTHON_SUITES)
    steps = [
        Step("whitespace", ("git", "diff", "--check", "HEAD")),
        Step("tracker-sync", ("python3", "tools/dev/sync_tracker.py"), skip_exit_code=EXIT_SKIPPED),
    ]
    format_paths = sorted(
        p for p in paths
        if Path(p).suffix in FORMAT_SUFFIXES and not p.startswith("third_party/")
    )
    if format_paths:
        steps.append(Step(
            "clang-format",
            ("xcrun", "clang-format", "--dry-run", "--Werror", *format_paths),
            requires="clang-format",
        ))
    if not configured:
        steps.append(Step("configure-release", ("cmake", "--preset", "release")))
    steps.append(Step("build-release", ("cmake", "--build", "--preset", "release")))
    if tidy_paths:
        steps.append(Step(
            "clang-tidy",
            ("clang-tidy", "-p", "build/release", "--quiet", "--warnings-as-errors=*", *tidy_paths),
            requires="clang-tidy",
        ))
    ctest = ("ctest", "--preset", "release")
    steps.append(Step("ctest-release", ctest if risks.timing else (*ctest, "-LE", "pts")))
    if risks.native:
        steps.append(Step("asan-ubsan", ("cmake", "--workflow", "--preset", "asan-ubsan")))
    if risks.concurrency:
        steps.append(Step("tsan", ("cmake", "--workflow", "--preset", "tsan")))
    if risks.authoring:
        for name, script in (
            ("node-toolchain", "check:toolchain"),
            ("contracts-check", "contracts:check"),
            ("contracts-test", "contracts:test"),
            ("authoring-typecheck", "check:authoring"),
            ("authoring-build", "build:authoring"),
            ("authoring-test", "test:authoring"),
        ):
            steps.append(Step(name, ("npm", "run", script), requires="npm"))
    for suite in risks.python_suites:
        steps.append(Step(
            f"python:{suite}",
            ("python3", "-m", "unittest", "discover", "-s", suite, "-p", "test_*.py"),
        ))
    return steps


def parse_ctest_summary(output: str) -> dict[str, int] | None:
    matches = CTEST_SUMMARY.findall(output)
    if not matches:
        return None
    _, failed, total = (int(value) for value in matches[-1])
    return {"total": total, "passed": total - failed, "failed": failed}


def changed_paths(base: str) -> list[str]:
    tracked = git_lines("diff", "--name-only", "--diff-filter=d", base)
    untracked = git_lines("ls-files", "--others", "--exclude-standard")
    return sorted(set(tracked) | set(untracked))


def git_lines(*args: str) -> list[str]:
    result = subprocess.run(
        ("git", *args), cwd=REPO_ROOT, check=True, capture_output=True, text=True
    )
    return [line for line in result.stdout.splitlines() if line]


def read_repo_text(path: str) -> str | None:
    try:
        return (REPO_ROOT / path).read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        return None


TOOL_OVERRIDES = {"clang-tidy": "CLANG_TIDY"}


def resolve_tool(tool: str, environ=os.environ) -> str | None:
    """Return the executable for tool, honoring an explicit environment override."""
    override = environ.get(TOOL_OVERRIDES.get(tool, ""))
    if override:
        return override if os.access(override, os.X_OK) else None
    return shutil.which(tool)


def tool_available(tool: str) -> bool:
    if tool == "clang-format":
        found = subprocess.run(
            ("xcrun", "--find", "clang-format"), capture_output=True, check=False
        ) if shutil.which("xcrun") else None
        return found is not None and found.returncode == 0
    return resolve_tool(tool) is not None


def tool_version(executable: str) -> str | None:
    result = subprocess.run((executable, "--version"), capture_output=True, text=True, check=False)
    match = re.search(r"LLVM version (\S+)", result.stdout)
    return match.group(1) if match else None


def run_step(step: Step) -> dict:
    record = {"name": step.name, "command": step.command()}
    if step.requires is not None and not tool_available(step.requires):
        return record | {"status": "unavailable", "reason": f"{step.requires} not found"}
    argv = step.argv
    if argv[0] in TOOL_OVERRIDES:
        executable = resolve_tool(argv[0])
        argv = (executable, *argv[1:])
        version = tool_version(executable)
        if version is not None:
            record["tool_version"] = version
    print(f"==> {step.name}: {step.command()}", flush=True)
    started = time.monotonic()
    process = subprocess.Popen(
        argv, cwd=REPO_ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    collected: list[str] = []
    assert process.stdout is not None
    for line in process.stdout:
        sys.stdout.write(line)
        collected.append(line)
    exit_code = process.wait()
    if exit_code == 0:
        status = "passed"
    elif exit_code == step.skip_exit_code:
        status = "skipped"
    else:
        status = "failed"
    record |= {
        "status": status,
        "exit_code": exit_code,
        "duration_s": round(time.monotonic() - started, 1),
    }
    summary = parse_ctest_summary("".join(collected))
    if summary is not None:
        record["tests"] = summary
    return record


def overall_result(records: list[dict]) -> str:
    statuses = {record["status"] for record in records}
    if "failed" in statuses:
        return "failed"
    if statuses & {"unavailable", "not_run"}:
        return "incomplete"
    return "passed"


def build_report(mode: str, base: str, paths: list[str], risks: Risks, records: list[dict]) -> dict:
    head = git_lines("rev-parse", "--short", "HEAD")[0]
    return {
        "schema_version": REPORT_SCHEMA_VERSION,
        "generated_by": "tools/dev/check.py",
        "generated_at_utc": datetime.datetime.now(datetime.UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "mode": mode,
        "base": base,
        "head": head,
        "changed_paths": paths,
        "risks": risks.labels(),
        "checks": records,
        "result": overall_result(records),
        "certified_performance": False,
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="HEAD", help="git revision to diff against (default: HEAD)")
    parser.add_argument("--gate", action="store_true", help="run the full phase-gate matrix")
    parser.add_argument("--asan", action="store_true", help="force the ASan/UBSan workflow")
    parser.add_argument("--tsan", action="store_true", help="force the TSan workflow")
    parser.add_argument("--pts", action="store_true", help="include long PTS conformance tests")
    parser.add_argument("--keep-going", action="store_true", help="run remaining checks after a failure")
    parser.add_argument("--dry-run", action="store_true", help="print the selected checks only")
    parser.add_argument("--report", type=Path, help="write a checks.json report to this path")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    paths = changed_paths(args.base)
    inferred = infer_risks(paths, read_repo_text)
    risks = dataclasses.replace(
        inferred,
        native=inferred.native or args.asan,
        concurrency=inferred.concurrency or args.tsan,
        timing=inferred.timing or args.pts,
    )
    configured = (REPO_ROOT / "build/release/CMakeCache.txt").is_file()
    tidy_paths = None
    if (REPO_ROOT / ".clang-tidy").is_file():
        if args.gate:
            from own_translation_units import list_own_translation_units

            compile_commands = REPO_ROOT / "build/release/compile_commands.json"
            tidy_paths = (
                list_own_translation_units(compile_commands=compile_commands)
                if compile_commands.is_file()
                else tidy_sources(git_lines("ls-files", "engine"))
            )
        else:
            tidy_paths = tidy_sources(paths)
    steps = plan_steps(
        paths, risks, gate=args.gate, configured=configured, tidy_paths=tidy_paths
    )
    mode = "gate" if args.gate else "change"
    print(f"changed paths: {len(paths)}; risks: {', '.join(risks.labels()) or 'none'}")
    if args.dry_run:
        for step in steps:
            print(f"  {step.name}: {step.command()}")
        return 0

    records: list[dict] = []
    failed = False
    for step in steps:
        if failed and not args.keep_going:
            records.append({"name": step.name, "command": step.command(), "status": "not_run"})
            continue
        record = run_step(step)
        records.append(record)
        failed = failed or record["status"] == "failed"

    report = build_report(mode, args.base, paths, risks, records)
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"result: {report['result']}")
    for record in records:
        print(f"  {record['status']:>11}  {record['name']}")
    return 0 if report["result"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
