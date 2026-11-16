import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import check


def names(steps):
    return [step.name for step in steps]


class InferRisksTest(unittest.TestCase):
    def test_documentation_only_change_has_no_risk(self):
        risks = check.infer_risks(["README.md", "docs/guide.md"])
        self.assertEqual(risks.labels(), [])

    def test_native_source_requires_address_sanitizers(self):
        risks = check.infer_risks(["engine/src/core-model/src/time.cpp"], lambda _: "int x;")
        self.assertTrue(risks.native)
        self.assertFalse(risks.concurrency)
        self.assertTrue(risks.timing)

    def test_runtime_path_or_threading_content_requires_thread_sanitizer(self):
        self.assertTrue(check.infer_risks(["engine/src/runtime/src/new.cpp"]).concurrency)
        threaded = check.infer_risks(
            ["engine/src/render-metal/src/owner.mm"], lambda _: "std::jthread worker;"
        )
        self.assertTrue(threaded.concurrency)
        self.assertFalse(threaded.timing)

    def test_build_files_are_native_but_authoring_manifests_are_not(self):
        self.assertTrue(check.infer_risks(["engine/src/CMakeLists.txt"]).native)
        self.assertTrue(check.infer_risks(["cmake/toolchain.cmake"]).native)
        authoring = check.infer_risks(["authoring/packages/sdk/src/index.ts", "package.json"])
        self.assertFalse(authoring.native)
        self.assertTrue(authoring.authoring)

    def test_python_tool_changes_select_their_suite(self):
        risks = check.infer_risks(["tools/benchmark/measure.py", "tools/dev/check.py"])
        self.assertEqual(risks.python_suites, ("tools/benchmark", "tools/dev"))
        self.assertFalse(risks.native)


class PlanStepsTest(unittest.TestCase):
    def test_minimal_change_runs_release_without_long_conformance(self):
        steps = check.plan_steps(["README.md"], check.Risks(), gate=False, configured=True)
        self.assertEqual(
            names(steps), ["whitespace", "tracker-sync", "build-release", "ctest-release"]
        )
        self.assertEqual(steps[-1].argv[-2:], ("-LE", "pts"))
        self.assertEqual(steps[1].skip_exit_code, check.EXIT_SKIPPED)

    def test_unconfigured_tree_is_configured_first(self):
        steps = check.plan_steps([], check.Risks(), gate=False, configured=False)
        self.assertEqual(names(steps)[2:4], ["configure-release", "build-release"])

    def test_clang_tidy_runs_only_with_configuration_and_translation_units(self):
        paths = ["engine/src/a.cpp", "engine/src/a.hpp", "engine/native/b.mm", "tools/x.cpp"]
        self.assertEqual(check.tidy_sources(paths), ["engine/native/b.mm", "engine/src/a.cpp"])
        without = check.plan_steps(paths, check.Risks(native=True), gate=False, configured=True)
        self.assertNotIn("clang-tidy", names(without))
        with_config = check.plan_steps(
            paths, check.Risks(native=True), gate=False, configured=True,
            tidy_paths=check.tidy_sources(paths),
        )
        tidy = next(step for step in with_config if step.name == "clang-tidy")
        self.assertEqual(tidy.argv[-2:], ("engine/native/b.mm", "engine/src/a.cpp"))
        self.assertEqual(tidy.requires, "clang-tidy")
        self.assertLess(names(with_config).index("build-release"), names(with_config).index("clang-tidy"))

    def test_native_change_formats_only_changed_sources(self):
        paths = ["engine/src/a.cpp", "engine/src/a.hpp", "engine/shaders/x.metal", "notes.md"]
        steps = check.plan_steps(paths, check.Risks(native=True), gate=False, configured=True)
        fmt = next(step for step in steps if step.name == "clang-format")
        self.assertEqual(fmt.argv[-2:], ("engine/src/a.cpp", "engine/src/a.hpp"))
        self.assertIn("asan-ubsan", names(steps))
        self.assertNotIn("tsan", names(steps))

    def test_gate_runs_the_full_matrix(self):
        steps = check.plan_steps([], check.Risks(), gate=True, configured=True)
        selected = names(steps)
        for required in ("asan-ubsan", "tsan", "authoring-typecheck", "python:tools/toolchain"):
            self.assertIn(required, selected)
        ctest = next(step for step in steps if step.name == "ctest-release")
        self.assertNotIn("-LE", ctest.argv)

    def test_commands_contain_no_absolute_paths(self):
        steps = check.plan_steps([], check.Risks(), gate=True, configured=False)
        for step in steps:
            self.assertFalse(any(arg.startswith("/") for arg in step.argv), step.command())


class ReportTest(unittest.TestCase):
    def test_ctest_summary_uses_last_summary_line(self):
        output = "x\n90% tests passed, 1 tests failed out of 10\n100% tests passed, 0 tests failed out of 12\n"
        self.assertEqual(check.parse_ctest_summary(output), {"total": 12, "passed": 12, "failed": 0})
        self.assertIsNone(check.parse_ctest_summary("Ran 3 tests\nOK\n"))

    def test_overall_result_distinguishes_unavailable_from_passed(self):
        self.assertEqual(check.overall_result([{"status": "passed"}]), "passed")
        self.assertEqual(
            check.overall_result([{"status": "passed"}, {"status": "unavailable"}]), "incomplete"
        )
        self.assertEqual(
            check.overall_result([{"status": "failed"}, {"status": "not_run"}]), "failed"
        )
        self.assertEqual(
            check.overall_result([{"status": "passed"}, {"status": "skipped"}]), "passed"
        )

    def test_clang_tidy_override_runs_without_recording_its_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            fake = Path(tmp) / "fake-tidy"
            fake.write_text("#!/bin/sh\necho 'Homebrew LLVM version 9.9.9'\n", encoding="utf-8")
            fake.chmod(0o755)
            self.assertEqual(check.resolve_tool("clang-tidy", {"CLANG_TIDY": str(fake)}), str(fake))
            fake.chmod(0o644)
            self.assertIsNone(check.resolve_tool("clang-tidy", {"CLANG_TIDY": str(fake)}))
            fake.chmod(0o755)
            step = check.Step("clang-tidy", ("clang-tidy", "engine/a.cpp"), requires="clang-tidy")
            with mock.patch.dict(os.environ, {"CLANG_TIDY": str(fake)}):
                record = check.run_step(step)
        self.assertEqual(record["status"], "passed")
        self.assertEqual(record["command"], "clang-tidy engine/a.cpp")
        self.assertEqual(record["tool_version"], "9.9.9")
        self.assertNotIn(tmp, json.dumps(record))

    def test_skip_exit_code_is_recorded_as_skipped(self):
        step = check.Step("skip", ("python3", "-c", "raise SystemExit(3)"), skip_exit_code=3)
        self.assertEqual(check.run_step(step)["status"], "skipped")
        failing = check.Step("fail", ("python3", "-c", "raise SystemExit(3)"))
        self.assertEqual(check.run_step(failing)["status"], "failed")


if __name__ == "__main__":
    unittest.main()
