#!/usr/bin/env python3
"""Run clang-tidy with warnings-as-errors on every own translation unit."""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
from pathlib import Path

from own_translation_units import REPO_ROOT, list_own_translation_units


def resolve_clang_tidy(explicit: str | None) -> str | None:
    if explicit:
        return explicit if os.access(explicit, os.X_OK) else None
    override = os.environ.get("CLANG_TIDY")
    if override:
        return override if os.access(override, os.X_OK) else None
    return shutil.which("clang-tidy")


def tidy_one(
    tidy: str,
    compile_commands_dir: Path,
    relative_path: str,
) -> tuple[str, int, str]:
    result = subprocess.run(
        (
            tidy,
            "-p",
            str(compile_commands_dir),
            "--quiet",
            "--warnings-as-errors=*",
            relative_path,
        ),
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    output = (result.stdout or "") + (result.stderr or "")
    return relative_path, result.returncode, output


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-p",
        "--compile-commands-dir",
        type=Path,
        required=True,
        help="build directory that contains compile_commands.json",
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=max(1, (os.cpu_count() or 2) // 2),
        help="parallel clang-tidy workers",
    )
    parser.add_argument(
        "--clang-tidy",
        default=None,
        help="clang-tidy executable (defaults to CLANG_TIDY or PATH)",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    compile_commands = args.compile_commands_dir / "compile_commands.json"
    if not compile_commands.is_file():
        print(f"missing compile_commands: {compile_commands}", file=sys.stderr)
        return 1
    tidy = resolve_clang_tidy(args.clang_tidy)
    if tidy is None:
        print("clang-tidy not found; set CLANG_TIDY or install Homebrew llvm", file=sys.stderr)
        return 1

    units = list_own_translation_units(compile_commands=compile_commands)
    if not units:
        print("no own translation units found", file=sys.stderr)
        return 1

    print(f"clang-tidy on {len(units)} own translation units (-j {args.jobs})", flush=True)
    failures: list[tuple[str, str]] = []
    completed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [
            pool.submit(tidy_one, tidy, args.compile_commands_dir, path)
            for path in units
        ]
        for future in concurrent.futures.as_completed(futures):
            path, code, output = future.result()
            completed += 1
            if code != 0:
                failures.append((path, output))
                print(f"FAIL {path}", flush=True)
            elif completed % 50 == 0 or completed == len(units):
                print(f"progress {completed}/{len(units)}", flush=True)

    if failures:
        print(f"{len(failures)} clang-tidy failure(s)", file=sys.stderr)
        for path, output in sorted(failures):
            print(f"===== {path} =====", file=sys.stderr)
            sys.stderr.write(output)
        return 1
    print("clang-tidy passed on all own translation units")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
