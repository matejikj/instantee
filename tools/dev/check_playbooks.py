#!/usr/bin/env python3
"""Dry-run checker for troubleshooting and benchmark playbooks.

Ensures every documented troubleshooting case includes reproduction steps and
diagnostics, and that the required common failure topics are present.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
PLAYBOOKS = REPO_ROOT / "playbooks"
TROUBLESHOOTING = PLAYBOOKS / "troubleshooting.md"
BENCHMARK = PLAYBOOKS / "benchmark.md"

REQUIRED_CASES = (
    "export_failure",
    "missing_or_corrupt_font",
    "color_mismatch",
    "gpu_device_loss",
    "memory_pressure",
    "measurement_invalid",
)

REQUIRED_SECTIONS = ("Symptoms", "Reproduction", "Diagnostics", "Next steps")
CASE_RE = re.compile(r"^## Case:\s*([a-z0-9_]+)\s*$", re.MULTILINE)
SECTION_RE = re.compile(r"^###\s+(.+?)\s*$", re.MULTILINE)


def parse_cases(text: str) -> dict[str, dict[str, str]]:
    matches = list(CASE_RE.finditer(text))
    cases: dict[str, dict[str, str]] = {}
    for index, match in enumerate(matches):
        name = match.group(1)
        start = match.end()
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        body = text[start:end]
        section_matches = list(SECTION_RE.finditer(body))
        parsed: dict[str, str] = {}
        for s_index, section in enumerate(section_matches):
            s_start = section.end()
            s_end = (
                section_matches[s_index + 1].start()
                if s_index + 1 < len(section_matches)
                else len(body)
            )
            parsed[section.group(1)] = body[s_start:s_end].strip()
        cases[name] = parsed
    return cases


def check_troubleshooting(path: Path) -> list[str]:
    errors: list[str] = []
    if not path.is_file():
        return [f"missing {path.relative_to(REPO_ROOT).as_posix()}"]
    text = path.read_text(encoding="utf-8")
    cases = parse_cases(text)
    for required in REQUIRED_CASES:
        if required not in cases:
            errors.append(f"troubleshooting missing case: {required}")
            continue
        for section in REQUIRED_SECTIONS:
            body = cases[required].get(section, "")
            if not body:
                errors.append(
                    f"case {required}: missing or empty section '{section}'"
                )
            elif section == "Reproduction" and "```" not in body:
                errors.append(
                    f"case {required}: Reproduction must include a fenced command block"
                )
            elif section == "Diagnostics" and len(body) < 40:
                errors.append(
                    f"case {required}: Diagnostics section looks too short"
                )
    return errors


def check_benchmark(path: Path) -> list[str]:
    errors: list[str] = []
    if not path.is_file():
        return [f"missing {path.relative_to(REPO_ROOT).as_posix()}"]
    text = path.read_text(encoding="utf-8").lower()
    for needle in (
        "certified",
        "sanitizer",
        "measure.py",
        "cold",
        "warm",
        "performance_claim",
    ):
        if needle not in text:
            errors.append(f"benchmark playbook should mention {needle!r}")
    if "never" not in text and "not" not in text:
        errors.append("benchmark playbook must warn against false certification")
    return errors


def run_checks() -> list[str]:
    errors: list[str] = []
    if not (PLAYBOOKS / "README.md").is_file():
        errors.append("missing playbooks/README.md")
    errors.extend(check_troubleshooting(TROUBLESHOOTING))
    errors.extend(check_benchmark(BENCHMARK))
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)
    errors = run_checks()
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        print("playbook dry-run failed", file=sys.stderr)
        return 1
    print(
        f"playbook dry-run ok (cases={len(REQUIRED_CASES)}, "
        f"benchmark=present)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
