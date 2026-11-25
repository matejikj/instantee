#!/usr/bin/env python3
"""Enforce quality lint and accessibility release policy on finished films.

Fails when a released example carries an unwaived error finding, when a
blocking code is present without an approved waiver, when legibility checklist
items are missing/failed, or when Style Bible packages lack a usable safe-area
token. Optionally cross-checks the native `instantee lint` evaluator.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_POLICY = REPO_ROOT / "contracts" / "quality-release" / "policy-v1.json"
SEVERITIES = {"info", "warning", "error"}


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def rel(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def waiver_matches(waiver: dict[str, Any], finding: dict[str, Any]) -> bool:
    if waiver.get("code") != finding.get("code"):
        return False
    path = waiver.get("path") or ""
    if path and path != finding.get("path"):
        return False
    element_id = waiver.get("element_id") or ""
    if element_id and element_id != finding.get("element_id"):
        return False
    return True


def evaluate_strict(
    findings: list[dict[str, Any]],
    waivers: list[dict[str, Any]],
) -> dict[str, Any]:
    """Mirror direction::evaluate_lint_policy for strict release builds."""
    errors: list[str] = []
    for finding in findings:
        for key in ("code", "severity", "path", "element_id"):
            if not finding.get(key):
                errors.append(f"invalid finding: missing {key}")
        if finding.get("severity") not in SEVERITIES:
            errors.append(
                f"invalid finding severity: {finding.get('severity')!r}"
            )
    for waiver in waivers:
        for key in ("code", "reason", "approval_id"):
            if not waiver.get(key):
                errors.append(f"invalid waiver: missing {key}")
    if errors:
        return {
            "ok": False,
            "blocking": [],
            "waived": [],
            "waivers_unused": [],
            "errors": errors,
        }

    waiver_used = [False] * len(waivers)
    blocking: list[dict[str, Any]] = []
    waived: list[dict[str, Any]] = []
    for finding in findings:
        matched: int | None = None
        for index, waiver in enumerate(waivers):
            if waiver_matches(waiver, finding):
                matched = index
                break
        if matched is not None:
            waiver_used[matched] = True
            waived.append({"finding": finding, "waiver": waivers[matched]})
            continue
        # Strict mode: unwaived errors block. Severity remains authoritative.
        if finding.get("severity") == "error":
            blocking.append(finding)

    unused = [waivers[i] for i, used in enumerate(waiver_used) if not used]
    return {
        "ok": not blocking and not unused and not errors,
        "blocking": blocking,
        "waived": waived,
        "waivers_unused": unused,
        "errors": errors,
    }


def checklist_errors(review: dict[str, Any], required_ids: list[str]) -> list[str]:
    if review.get("status") != "approved":
        return [f"visual review status is {review.get('status')!r}, expected approved"]
    results = {
        item.get("id"): item
        for item in (review.get("checklist_results") or [])
        if item.get("id")
    }
    errors: list[str] = []
    for item_id in required_ids:
        item = results.get(item_id)
        if item is None:
            errors.append(f"missing legibility checklist result: {item_id}")
        elif not item.get("passed"):
            errors.append(f"legibility checklist failed: {item_id}")
    return errors


def safe_area_errors(style_doc: dict[str, Any], label: str) -> list[str]:
    tokens = style_doc.get("tokens")
    if not isinstance(tokens, dict):
        return [f"{label}: missing tokens object"]
    safe_area = tokens.get("safe_area")
    if not isinstance(safe_area, dict):
        return [f"{label}: missing tokens.safe_area"]
    errors: list[str] = []
    if not safe_area.get("mode"):
        errors.append(f"{label}: tokens.safe_area.mode is required")
    for edge in ("top_px", "bottom_px", "left_px", "right_px"):
        value = safe_area.get(edge)
        if not isinstance(value, (int, float)) or value < 0:
            errors.append(
                f"{label}: tokens.safe_area.{edge} must be a non-negative number"
            )
    # At least one inset must be positive so the token is usable on phone UI.
    insets = [
        safe_area.get(edge, 0)
        for edge in ("top_px", "bottom_px", "left_px", "right_px")
    ]
    if all(isinstance(v, (int, float)) and v == 0 for v in insets):
        errors.append(f"{label}: tokens.safe_area insets must not all be zero")
    return errors


def load_waivers(repo_root: Path, example: dict[str, Any]) -> list[dict[str, Any]]:
    waivers_path = example.get("waivers")
    if not waivers_path:
        return []
    doc = load_json(repo_root / waivers_path)
    if doc.get("schema_version") != "lint-waivers/1":
        raise ValueError(f"{waivers_path}: expected schema_version lint-waivers/1")
    waivers = doc.get("waivers")
    if not isinstance(waivers, list):
        raise ValueError(f"{waivers_path}: waivers must be an array")
    return waivers


def verify_example(
    repo_root: Path,
    policy: dict[str, Any],
    example: dict[str, Any],
) -> dict[str, Any]:
    example_id = example.get("id", "<unknown>")
    result: dict[str, Any] = {
        "id": example_id,
        "ok": True,
        "errors": [],
        "lint": None,
        "legibility": None,
    }
    blocking_codes = set(policy.get("blocking_codes") or [])

    findings_rel = example.get("findings")
    if not findings_rel:
        result["ok"] = False
        result["errors"].append("missing findings path")
        return result

    findings_path = repo_root / findings_rel
    if not findings_path.is_file():
        result["ok"] = False
        result["errors"].append(f"missing findings file: {findings_rel}")
        return result

    findings_doc = load_json(findings_path)
    if findings_doc.get("schema_version") != "lint-findings/1":
        result["ok"] = False
        result["errors"].append(
            f"{findings_rel}: expected schema_version lint-findings/1"
        )
        return result
    findings = findings_doc.get("findings")
    if not isinstance(findings, list):
        result["ok"] = False
        result["errors"].append(f"{findings_rel}: findings must be an array")
        return result

    try:
        waivers = load_waivers(repo_root, example)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        result["ok"] = False
        result["errors"].append(str(exc))
        return result

    for finding in findings:
        if (
            finding.get("severity") == "error"
            and finding.get("code") not in blocking_codes
        ):
            result["ok"] = False
            result["errors"].append(
                "error finding uses code outside blocking_codes: "
                f"{finding.get('code')}"
            )

    lint = evaluate_strict(findings, waivers)
    result["lint"] = {
        "ok": lint["ok"],
        "blocking_count": len(lint["blocking"]),
        "waived_count": len(lint["waived"]),
        "unused_waiver_count": len(lint["waivers_unused"]),
        "blocking": lint["blocking"],
        "errors": lint["errors"],
    }
    if not lint["ok"]:
        result["ok"] = False
        if lint["blocking"]:
            codes = sorted({f.get("code", "?") for f in lint["blocking"]})
            result["errors"].append(
                f"unapproved error findings: {', '.join(codes)}"
            )
        if lint["waivers_unused"]:
            result["errors"].append("unused lint waivers present")
        result["errors"].extend(lint["errors"])

    legibility = example.get("legibility") or {}
    kind = legibility.get("kind")
    legibility_errors: list[str] = []
    if kind == "visual-review":
        review_rel = legibility.get("review_record")
        required = list(legibility.get("required_checklist_ids") or [])
        if not review_rel:
            legibility_errors.append("missing review_record")
        else:
            review_path = repo_root / review_rel
            if not review_path.is_file():
                legibility_errors.append(f"missing review record: {review_rel}")
            else:
                legibility_errors.extend(
                    checklist_errors(load_json(review_path), required)
                )
    elif kind == "style-bible-safe-area":
        packages = list(legibility.get("style_packages") or [])
        if not packages:
            legibility_errors.append("style_packages must be non-empty")
        for package_rel in packages:
            package_path = repo_root / package_rel
            if not package_path.is_file():
                legibility_errors.append(f"missing style package: {package_rel}")
                continue
            legibility_errors.extend(
                safe_area_errors(load_json(package_path), package_rel)
            )
        entry = example.get("example_entry")
        if entry and not (repo_root / entry).is_file():
            legibility_errors.append(f"missing authoring example entry: {entry}")
    else:
        legibility_errors.append(f"unsupported legibility kind: {kind!r}")

    result["legibility"] = {"ok": not legibility_errors, "errors": legibility_errors}
    if legibility_errors:
        result["ok"] = False
        result["errors"].extend(legibility_errors)
    return result


def cross_check_cli(
    cli: Path,
    findings_path: Path,
    waivers_path: Path | None,
    *,
    expect_blocking: bool,
) -> list[str]:
    command = [str(cli), "lint", str(findings_path), "--policy", "strict"]
    if waivers_path is not None:
        command.extend(["--waivers", str(waivers_path)])
    completed = subprocess.run(
        command, cwd=REPO_ROOT, capture_output=True, text=True, check=False
    )
    if completed.returncode not in (0, 2):
        return [
            f"cli lint unexpected exit {completed.returncode}: "
            f"{completed.stderr.strip() or completed.stdout.strip()}"
        ]
    try:
        report = json.loads(completed.stdout.strip().splitlines()[-1])
    except (json.JSONDecodeError, IndexError) as exc:
        return [f"cli lint report parse failed: {exc}"]
    cli_blocking = bool(report.get("blocking"))
    if cli_blocking != expect_blocking:
        return [
            "cli/python blocking decision mismatch "
            f"(cli_blocking={cli_blocking}, expect_blocking={expect_blocking})"
        ]
    return []


def validate_policy(policy: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if policy.get("schema_version") != 1:
        errors.append("policy schema_version must be 1")
    if policy.get("enforcement") != "strict":
        errors.append("policy enforcement must be strict")
    for key in ("blocking_codes", "warning_codes", "released_examples"):
        if key not in policy:
            errors.append(f"policy missing {key}")
    blocking = policy.get("blocking_codes")
    warning = policy.get("warning_codes")
    if not isinstance(blocking, list) or not blocking:
        errors.append("blocking_codes must be a non-empty array")
    if not isinstance(warning, list) or not warning:
        errors.append("warning_codes must be a non-empty array")
    examples = policy.get("released_examples")
    if not isinstance(examples, list) or not examples:
        errors.append("released_examples must be a non-empty array")
    return errors


def run_policy(
    repo_root: Path,
    policy_path: Path,
    *,
    cli: Path | None = None,
) -> dict[str, Any]:
    policy = load_json(policy_path)
    report: dict[str, Any] = {
        "schema_version": 1,
        "policy": rel(policy_path, repo_root),
        "enforcement": policy.get("enforcement"),
        "blocking_codes": list(policy.get("blocking_codes") or []),
        "warning_codes": list(policy.get("warning_codes") or []),
        "ok": True,
        "examples": [],
        "errors": [],
        "certified_performance": False,
    }
    policy_errors = validate_policy(policy)
    if policy_errors:
        report["ok"] = False
        report["errors"].extend(policy_errors)
        return report

    for example in policy.get("released_examples") or []:
        example_result = verify_example(repo_root, policy, example)
        report["examples"].append(example_result)
        if not example_result["ok"]:
            report["ok"] = False
            for err in example_result["errors"]:
                report["errors"].append(f"{example_result['id']}: {err}")

        if cli is not None and example.get("findings"):
            findings_path = repo_root / example["findings"]
            waivers_rel = example.get("waivers")
            waivers_path = repo_root / waivers_rel if waivers_rel else None
            # Native ok ignores unused waivers; compare blocking emptiness only.
            lint = example_result.get("lint") or {}
            expect_blocking = lint.get("blocking_count", 0) > 0
            cli_errors = cross_check_cli(
                cli,
                findings_path,
                waivers_path,
                expect_blocking=expect_blocking,
            )
            if cli_errors:
                report["ok"] = False
                report["errors"].extend(
                    f"{example_result['id']}: {err}" for err in cli_errors
                )

    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Check quality lint and accessibility release policy"
    )
    parser.add_argument(
        "--policy",
        type=Path,
        default=DEFAULT_POLICY,
        help="path to quality-release policy JSON",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=REPO_ROOT,
        help="repository root",
    )
    parser.add_argument(
        "--write-report",
        type=Path,
        default=None,
        help="write quality-lint-report.json to this path",
    )
    parser.add_argument(
        "--cli",
        type=Path,
        default=None,
        help="optional instantee binary for lint cross-check",
    )
    args = parser.parse_args(argv)

    policy_path = args.policy
    if not policy_path.is_file():
        print(f"missing policy: {policy_path}", file=sys.stderr)
        return 1

    cli = args.cli
    if cli is not None and not cli.is_file():
        print(f"missing cli binary: {cli}", file=sys.stderr)
        return 1

    report = run_policy(args.repo_root, policy_path, cli=cli)
    body = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.write_report is not None:
        args.write_report.parent.mkdir(parents=True, exist_ok=True)
        args.write_report.write_text(body, encoding="utf-8")

    if report["ok"]:
        count = len(report["examples"])
        print(f"quality release ok ({count} examples)")
        return 0

    for err in report["errors"]:
        print(err, file=sys.stderr)
    print("quality release failed", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
