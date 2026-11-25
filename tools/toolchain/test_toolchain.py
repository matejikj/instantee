"""Exercise native configuration failure modes without changing the working tree."""

import json
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ToolchainTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="instantee-toolchain-")
        self.addCleanup(self.temporary.cleanup)
        self.source = Path(self.temporary.name) / "source"
        self.source.mkdir()
        for name in ("CMakeLists.txt", "CMakePresets.json", "package.json"):
            shutil.copy2(ROOT / name, self.source / name)
        shutil.copytree(ROOT / "cmake", self.source / "cmake")
        # Editorial/notebook fixtures resolve assets via PROJECT_SOURCE_DIR.
        for name in (
            "engine",
            "authoring",
            "benchmarks",
            "third_party",
            "contracts",
            "assets",
        ):
            shutil.copytree(ROOT / name, self.source / name)
        # Math grammar (and other style probes) assert ADR presence under the
        # design pack. Copy ADRs when present so the sandboxed ctest matrix
        # matches a developer tree that includes the ignored design pack.
        adr_src = ROOT / "motion_framework_sas" / "adrs"
        if adr_src.is_dir():
            adr_dst = self.source / "motion_framework_sas" / "adrs"
            adr_dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(adr_src, adr_dst)
        helper = self.source / "tools/toolchain"
        helper.mkdir(parents=True)
        shutil.copy2(ROOT / "tools/toolchain/check-node.cjs", helper)
        workspace_tools = self.source / "tools/workspace"
        workspace_tools.mkdir(parents=True)
        shutil.copy2(ROOT / "tools/workspace/supervise.py", workspace_tools)
        shutil.copytree(ROOT / "tools/benchmark", self.source / "tools/benchmark")
        # Boundary, API-surface and shader-gate CTests invoke these scripts.
        dev_tools = self.source / "tools/dev"
        dev_tools.mkdir(parents=True)
        for script in (
            "check_module_boundaries.py",
            "check_cpp_api_surface.py",
            "check_shader_pin.py",
            "check_shader_layouts.py",
            "check_visual_baseline.py",
        ):
            shutil.copy2(ROOT / "tools/dev" / script, dev_tools)
        (self.source / "node_modules").symlink_to(ROOT / "node_modules", target_is_directory=True)
        self.build = Path(self.temporary.name) / "build"

    def run_command(self, arguments, timeout=300):
        return subprocess.run(
            arguments, cwd=self.source, text=True, capture_output=True, timeout=timeout
        )

    def configure(self, *flags):
        return self.run_command(["cmake", "--preset", "toolchain", "-B", str(self.build), *flags])

    def assert_success(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def assert_rejected(self, result, diagnostic):
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(diagnostic, result.stdout + result.stderr)
        self.assertIn("Configuring incomplete", result.stdout + result.stderr)

    def test_clean_build_and_run_both_languages_without_extensions(self):
        self.assert_success(self.configure())
        self.assert_success(self.run_command(["cmake", "--build", str(self.build)]))
        self.assert_success(self.run_command(["ctest", "--test-dir", str(self.build), "--output-on-failure"]))
        commands = json.loads((self.build / "compile_commands.json").read_text())
        probes = [item for item in commands if "probe_" in item["command"]]
        self.assertEqual(len(probes), 8)
        for command in commands:
            path = Path(command["file"])
            with self.subTest(file=path.name):
                # Vendored C sources keep -std=c11; C++/ObjC++ stay on C++23.
                if path.suffix == ".c":
                    self.assertIn("-std=c11", command["command"])
                    self.assertIn("-arch arm64", command["command"])
                    continue
                self.assertIn("-std=c++23", command["command"])
                self.assertNotIn("-std=gnu++", command["command"])
                self.assertIn("-arch arm64", command["command"])
                if command["file"].endswith(".mm"):
                    self.assertIn("-fobjc-arc", command["command"])
        parser = next(item for item in commands if item["file"].endswith("apps/cli/cli.cpp"))
        self.assertNotIn("native/export-apple", parser["command"])
        adapter = next(item for item in commands if item["file"].endswith("export-apple/src/workspace.mm"))
        self.assertNotIn("apps/cli", adapter["command"])
        contract = next(item for item in commands if item["file"].endswith("contracts/public_headers.cpp"))
#Inspect actual transitive includes, not just spelling in public headers.
        arguments = shlex.split(contract["command"])
        output = arguments.index("-o")
        del arguments[output:output + 2]
        arguments.remove("-c")
        dependencies = self.run_command([*arguments, "-M"])
        self.assert_success(dependencies)
        for platform in ("Foundation.framework", "CoreFoundation.framework", "Metal.framework",
                         "CoreVideo.framework", "AVFoundation.framework", "IOSurface.framework",
                         "/native/export-apple/src/", "/third_party/metal-cpp/", "/apps/cli/",
                         "/objc/", "Metal/Metal.hpp"):
            self.assertNotIn(platform, dependencies.stdout)

    def test_private_headers_are_not_part_of_public_api(self):
        self.assert_success(self.configure())
        command = ["cmake", "--build", str(self.build), "--target", "spike_public_headers_test"]
        self.assert_success(self.run_command(command))
        source = self.source / "engine/tests/contracts/public_headers.cpp"
        original = source.read_text()
        for header in ("cf_owner.hpp", "cli.hpp", "metal_owner.hpp",
                       "device_native.hpp", "Metal/Metal.hpp",
                       "readback_impl.hpp", "pixel_buffer_native.hpp",
                       "shared_surface_pattern_source.hpp"):
            with self.subTest(header=header):
                source.write_text(f'#include "{header}"\n' + original)
                rejected = self.run_command(command)
                self.assertNotEqual(rejected.returncode, 0)
                self.assertIn(f"'{header}' file not found", rejected.stdout + rejected.stderr)

    def test_benchmark_requires_successful_cpu_and_smoke_preflight(self):
        test = self.source / "engine/tests/unit/cli/cli_test.cpp"
        original = test.read_text()
        test.write_text("int main() { return 1; }\n")
        self.assert_success(self.configure())
        arguments = ["cmake", "--build", str(self.build), "--target", "run-spike-benchmark"]
        rejected = self.run_command(arguments)
        self.assertNotEqual(rejected.returncode, 0)
        self.assertIn("spike_cli_unit", rejected.stdout)
        self.assertIn("tests failed", rejected.stdout)
        self.assertNotIn('"code":"benchmark_unavailable"', rejected.stdout + rejected.stderr)
        test.write_text(original)
        result = self.run_command(arguments)
#Preflight passes, but the unimplemented benchmark must still fail honestly.
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("100% tests passed", result.stdout)
        self.assertIn('"code":"benchmark_unavailable"', result.stdout + result.stderr)

    def test_unknown_sanitizer_is_rejected(self):
        self.assert_rejected(self.configure("-DINSTANTEE_SANITIZER=invalid"),
                             "Unsupported INSTANTEE_SANITIZER")

    def test_missing_library_headers_are_configure_errors_in_each_language(self):
        for language in ("CXX", "OBJCXX"):
            for header in ("expected", "thread", "stop_token", "condition_variable"):
                with self.subTest(language=language, header=header):
                    self.build = Path(self.temporary.name) / f"{language}-{header}"
                    shadow = Path(self.temporary.name) / f"shadow-{language}-{header}"
                    shadow.mkdir()
                    (shadow / header).write_text('#error "deliberately unavailable library feature"\n')
                    flags = "-stdlib=libc++ " + ("-fobjc-arc " if language == "OBJCXX" else "")
                    result = self.configure(f"-DCMAKE_{language}_FLAGS={flags}-I{shadow}")
                    self.assert_rejected(result, f"Required {language} feature")
                    self.assertIn("deliberately unavailable library feature", result.stderr)

    def test_reconfiguration_does_not_reuse_successful_probe_results(self):
        shadow = Path(self.temporary.name) / "shadow"
        shadow.mkdir()
        flags = f"-DCMAKE_CXX_FLAGS=-stdlib=libc++ -I{shadow}"
        self.assert_success(self.configure(flags))
        (shadow / "expected").write_text('#error "feature removed after initial configure"\n')
        self.assert_rejected(self.configure(flags), "Required CXX feature expected failed to compile")

    def test_nonzero_probe_result_is_a_configure_error(self):
        (self.source / "cmake/probes/expected.cpp").write_text("int main() { return 4; }\n")
        self.assert_rejected(self.configure(), "Required CXX feature expected failed to run: 4")

    def test_hung_probe_is_a_bounded_configure_error(self):
        (self.source / "cmake/probes/expected.cpp").write_text(
            "#include <chrono>\n#include <thread>\n"
            "int main() { for (;;) std::this_thread::sleep_for(std::chrono::seconds{1}); }\n"
        )
        result = self.configure()
        self.assert_rejected(result, "Required CXX feature expected failed to run")
        self.assertIn("timeout", result.stderr.lower())

    def test_deployment_target_drift_is_rejected(self):
        self.assert_rejected(self.configure("-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0"), "deployment target differs")

    def test_ninja_version_drift_is_rejected(self):
        lock = self.source / "cmake/ToolchainLock.cmake"
        lock.write_text(lock.read_text().replace('"1.13.2"', '"0.0.0"'))
        self.assert_rejected(self.configure(), "Ninja differs from the toolchain lock")

    def test_node_version_drift_is_rejected(self):
        path = self.source / "package.json"
        lock = json.loads(path.read_text())
        lock["engines"]["node"] = "0.0.0"
        path.write_text(json.dumps(lock))
        self.assert_rejected(self.configure(), "Node/TypeScript lock")


if __name__ == "__main__":
    unittest.main()
