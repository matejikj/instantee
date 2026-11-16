"""Check process results, diagnostics and absence of output side effects."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


def check(binary, arguments, directory, expected):
    result = subprocess.run([binary, *arguments], cwd=directory, capture_output=True,
                            text=True, timeout=5)
    assert result.returncode == expected, (arguments, result.returncode, result.stderr)
    return result


def assert_metal_report(payload):
    metal = payload["metal"]
    assert isinstance(metal["name"], str) and metal["name"]
    assert isinstance(metal["architecture"], str)
    assert isinstance(metal["recommended_max_working_set_bytes"], int)
    assert metal["recommended_max_working_set_bytes"] > 0
    assert isinstance(metal["max_buffer_length_bytes"], int)
    assert metal["max_buffer_length_bytes"] > 0
    assert metal["supports_apple7"] is True
    assert metal["has_command_queue"] is True
    assert isinstance(metal["has_unified_memory"], bool)
    assert isinstance(metal["is_low_power"], bool)
    assert isinstance(metal["is_headless"], bool)
    assert isinstance(metal["supports_metal3"], bool)
    assert "registry_id" not in metal


def main():
    with tempfile.TemporaryDirectory(prefix="instantee-cli-") as directory:
        for binary, code, unavailable in ((sys.argv[1], 5, "renderer_unavailable"),
                                           (sys.argv[2], 2, "benchmark_unavailable")):
            help_result = check(binary, ["--help"], directory, 0)
            assert Path(binary).name in help_result.stdout and not help_result.stderr
            smoke = check(binary, ["--smoke"], directory, 0)
            payload = json.loads(smoke.stdout)
            assert payload["workspace"] == "ok"
            assert payload["published"] is False
            assert payload["certified"] is False
            assert_metal_report(payload)
            assert not smoke.stderr
            # A read-only descriptor makes the buffered stdout flush fail with EBADF.
            for option in ("--help", "--smoke"):
                with open("/dev/null", "rb") as unwritable:
                    failed_output = subprocess.run([binary, option], cwd=directory,
                                                   stdout=unwritable, stderr=subprocess.PIPE,
                                                   text=True, timeout=5)
                assert failed_output.returncode == 11
                assert json.loads(failed_output.stderr)["code"] == "stdout_failed"
            for arguments in ([], ["--unknown"], ["--help", "--smoke"],
                              ["--smoke", "--smoke"], [""], ["private-input"],
                              ["--help"] * 1024):
                result = check(binary, arguments, directory, code if not arguments else 2)
                diagnostic = json.loads(result.stderr)
                assert not result.stdout
                assert diagnostic["code"] == (unavailable if not arguments else "invalid_arguments")
                assert diagnostic["phase"] == (("render" if code == 5 else "benchmark")
                                               if not arguments else "arguments")
                assert diagnostic["published"] is False
                assert diagnostic["durability"] == "not_published"
                assert "private-input" not in result.stderr
        assert not list(Path(directory).iterdir())


if __name__ == "__main__":
    main()
