"""CLI e2e regression for scriptable shell/CI use and stable exit codes."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def run(binary: str, args: list[str], cwd: Path | None = None,
        timeout: float = 120) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [binary, *args],
        cwd=cwd,
        capture_output=True,
        text=True,
        timeout=timeout,
    )


def expect_exit(result: subprocess.CompletedProcess[str], code: int,
                label: str) -> None:
    if result.returncode != code:
        raise AssertionError(
            f"{label}: expected exit {code}, got {result.returncode}; "
            f"stderr={result.stderr!r} stdout={result.stdout!r}"
        )


def parse_json_line(text: str) -> dict:
    line = text.strip().splitlines()[-1]
    return json.loads(line)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: cli_e2e_regression.py <source_root> <instantee_bin>",
              file=sys.stderr)
        return 1

    root = Path(sys.argv[1])
    binary = sys.argv[2]
    authoring = root / "contracts/fixtures/authoring/positive/minimal.authoring.json"
    scene = root / "contracts/fixtures/scene-ir/positive/e2e_short.scene.json"
    bad_intent = (
        root / "contracts/fixtures/authoring/negative/unknown-intent.authoring.json"
    )
    bad_schema = (
        root /
        "contracts/fixtures/authoring/negative/unsupported-schema.authoring.json"
    )

    with tempfile.TemporaryDirectory(prefix="instantee-cli-e2e-") as tmp:
        tmp_path = Path(tmp)
        scene_out = tmp_path / "compiled.scene.json"
        mp4_out = tmp_path / "clip.mp4"
        report_out = tmp_path / "render-report.json"
        bench_out = tmp_path / "bench.json"

        help_result = run(binary, ["help"])
        expect_exit(help_result, 0, "help")
        assert "render" in help_result.stdout
        assert "benchmark" in help_result.stdout
        assert "--json-log" in help_result.stdout

        empty = run(binary, [])
        expect_exit(empty, 2, "missing command")
        diag = parse_json_line(empty.stderr) if empty.stderr.strip() else None
        # usage-only path has no stderr JSON; accept either usage or diagnostic.
        assert empty.stdout or empty.stderr

        unknown = run(binary, ["not-a-command"])
        expect_exit(unknown, 2, "unknown command")

        validate = run(binary, ["--json-log", "validate", str(authoring)])
        expect_exit(validate, 0, "validate")
        payload = parse_json_line(validate.stdout)
        assert payload["ok"] is True and payload["command"] == "validate"
        log_lines = [json.loads(line) for line in validate.stderr.splitlines()
                     if line.strip()]
        assert any(line.get("event") == "ok" for line in log_lines)

        bad = run(binary, ["validate", str(bad_intent)])
        expect_exit(bad, 2, "unsupported intent")
        bad_diag = parse_json_line(bad.stderr)
        assert bad_diag["published"] is False
        assert "durability" in bad_diag

        schema = run(binary, ["validate", str(bad_schema)])
        expect_exit(schema, 4, "incompatible schema")

        compile_result = run(
            binary,
            ["compile", str(authoring), "--out", str(scene_out)],
        )
        expect_exit(compile_result, 0, "compile")
        assert scene_out.is_file()

        frame = run(binary, ["frame", str(scene), "--at", "0"])
        expect_exit(frame, 0, "frame")
        frame_payload = parse_json_line(frame.stdout)
        assert frame_payload["command"] == "frame"
        assert frame_payload["published"] is False

        render = run(
            binary,
            [
                "--json-log",
                "render",
                str(scene),
                "--out",
                str(mp4_out),
                "--report",
                str(report_out),
                "--frames",
                "2",
                "--progress",
            ],
            timeout=180,
        )
        expect_exit(render, 0, "render")
        assert mp4_out.is_file() and mp4_out.stat().st_size > 0
        render_payload = parse_json_line(render.stdout)
        assert render_payload["ok"] is True
        assert render_payload["command"] == "render"
        assert render_payload["published"] is True
        assert render_payload["frames_appended"] == 2
        assert report_out.is_file()
        progress = [
            json.loads(line)
            for line in render.stderr.splitlines()
            if '"event":"progress"' in line
        ]
        assert len(progress) >= 1
        assert progress[-1]["frame"] == 2 and progress[-1]["total"] == 2

        # no-replace must refuse an existing target with a defined exit code
        conflict = run(
            binary,
            ["render", str(scene), "--out", str(mp4_out), "--frames", "1"],
            timeout=180,
        )
        expect_exit(conflict, 9, "publication conflict")
        conflict_diag = parse_json_line(conflict.stderr)
        assert conflict_diag["published"] is False

        bench = run(
            binary,
            [
                "benchmark",
                str(scene),
                "--preset",
                "m1-air",
                "--out",
                str(bench_out),
                "--frames",
                "2",
            ],
        )
        expect_exit(bench, 0, "benchmark scene")
        bench_payload = parse_json_line(bench.stdout)
        assert bench_payload["command"] == "benchmark"
        assert bench_payload["preset"] == "m1-air"
        assert bench_payload["certified"] is False
        assert bench_payload["input_kind"] == "scene"
        assert bench_out.is_file()

        bench_auth = run(
            binary,
            ["benchmark", str(authoring), "--preset", "m1-air", "--frames", "1"],
        )
        expect_exit(bench_auth, 0, "benchmark authoring")
        auth_payload = parse_json_line(bench_auth.stdout)
        assert auth_payload["input_kind"] == "authoring"
        assert auth_payload["certified"] is False

        bad_preset = run(
            binary,
            ["benchmark", str(scene), "--preset", "not-a-preset"],
        )
        expect_exit(bad_preset, 2, "unknown preset")

        missing = run(
            binary,
            ["render", str(tmp_path / "missing.scene.json"), "--out",
             str(tmp_path / "x.mp4")],
        )
        expect_exit(missing, 2, "missing scene")
        missing_diag = parse_json_line(missing.stderr)
        assert missing_diag["published"] is False
        assert "durability" in missing_diag

        # Ensure the temp workspace stayed under our control (no unexpected
        # absolute host paths leaked into JSON diagnostics).
        home = os.path.expanduser("~")
        for blob in (render.stdout, render.stderr, bench.stdout, missing.stderr):
            assert home not in blob

    print("cli_e2e_regression: ok")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
