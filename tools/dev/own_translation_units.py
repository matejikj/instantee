#!/usr/bin/env python3
"""Enumerate own native translation units suitable for clang-tidy."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
TIDY_SUFFIXES = {".c", ".cc", ".cpp", ".m", ".mm"}
EXCLUDED_SUBSTRINGS = ("/third_party/", "/metal-cpp/")


def _git_ls_files(*paths: str, repo_root: Path = REPO_ROOT) -> list[str]:
    result = subprocess.run(
        ("git", "ls-files", *paths),
        cwd=repo_root,
        check=True,
        capture_output=True,
        text=True,
    )
    return [line for line in result.stdout.splitlines() if line]


def _compile_command_files(compile_commands: Path) -> set[Path]:
    entries = json.loads(compile_commands.read_text(encoding="utf-8"))
    resolved: set[Path] = set()
    for entry in entries:
        file_field = entry.get("file")
        if not file_field:
            continue
        path = Path(file_field)
        if not path.is_absolute():
            directory = entry.get("directory")
            path = Path(directory, file_field) if directory else path
        resolved.add(path.resolve())
    return resolved


def is_excluded(relative_path: str) -> bool:
    return any(token in relative_path for token in EXCLUDED_SUBSTRINGS)


def list_own_translation_units(
    *,
    repo_root: Path = REPO_ROOT,
    compile_commands: Path | None = None,
) -> list[str]:
    """Return sorted repo-relative own TUs present in compile_commands.json.

    Own code means tracked files under ``engine/`` with tidy-capable suffixes,
    excluding vendored trees. Intersection with compile_commands keeps analysis
    limited to units the active build can actually compile.
    """
    tracked = _git_ls_files("engine", repo_root=repo_root)
    candidates = [
        path
        for path in tracked
        if Path(path).suffix in TIDY_SUFFIXES and not is_excluded(path)
    ]
    if compile_commands is None:
        return sorted(candidates)

    known = _compile_command_files(compile_commands)
    selected: list[str] = []
    for relative in candidates:
        absolute = (repo_root / relative).resolve()
        if absolute in known:
            selected.append(relative)
    return sorted(selected)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-p",
        "--compile-commands-dir",
        type=Path,
        help="build directory that contains compile_commands.json",
    )
    parser.add_argument(
        "--compile-commands",
        type=Path,
        help="explicit path to compile_commands.json",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    compile_commands: Path | None = args.compile_commands
    if compile_commands is None and args.compile_commands_dir is not None:
        compile_commands = args.compile_commands_dir / "compile_commands.json"
    if compile_commands is not None and not compile_commands.is_file():
        print(f"missing compile_commands: {compile_commands}", file=sys.stderr)
        return 1
    for path in list_own_translation_units(compile_commands=compile_commands):
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
