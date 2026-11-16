# Development guide

The current native workspace targets the pinned arm64 macOS/Xcode toolchain.
It is a feasibility build, not a portable renderer or a performance-certified
release. Exact versions and requirements are documented in the
[toolchain guide](tools/toolchain/README.md) and enforced by CMake and Node checks.
Python 3 is required for subprocess smoke and build integration tests.
The TypeScript authoring workspace can be checked independently of Xcode;
native build commands require the pinned macOS toolchain.

## Build and run

From the repository root with the required Node/npm and selected Xcode:

```sh
npm ci --ignore-scripts --no-audit --no-fund
npm run check:toolchain
npm run check:authoring
npm run build:authoring
cmake --workflow --preset release
build/release/spike-render --help
build/release/spike-render --smoke
build/release/spike-benchmark --smoke
```

The workflow configures, builds and runs CTest. Both executables currently
support help and a synchronous native smoke check. Actual rendering and
measurement return explicit unavailable errors. See the
[CLI/workspace guide](tools/workspace/README.md) for exit codes and the checked
benchmark entry point.

Code is divided by responsibility: [engine/](engine/README.md) owns the native
runtime, [authoring/](authoring/README.md) owns the TypeScript SDK, and
[contracts/](contracts/README.md) owns the shared data boundary. The authoring
package is currently a buildable shell without an implemented SDK API.

## Verification

Run the diagnostic workflows separately:

```sh
cmake --workflow --preset asan-ubsan
cmake --workflow --preset tsan
python3 -m unittest discover -s tools/toolchain -p 'test_*.py' -v
python3 -m unittest discover -s tools/machine-profile -p 'test_*.py' -v
git diff --check
```

The presets use separate build directories. Sanitized runs are correctness
diagnostics and must not be presented as release performance measurements.
System Apple frameworks are not fully instrumented by the project's sanitizers.
When only rerunning an existing configuration, use `cmake --build --preset NAME`
and `ctest --preset NAME`.

Format changed C++/Objective-C++ headers and sources using `xcrun clang-format`;
verify them with `--dry-run --Werror`. Use Clang static analysis or the project's
available lint tooling and report unavailable checks. Run relevant behavioral
tests before committing. Moving files or changing CMake include paths requires
build integration and public-header boundary checks as well as runtime tests.

The ignored planning pack has additional contract checks for contributors who
have it locally. It is not required to configure, build or test this workspace.

## Adding code

Follow [the repository structure and dependency rules](ARCHITECTURE.md). Keep
the root build short and define targets in their owning directory. Use
target-specific include paths and explicit public/private dependencies.
Public library headers belong under `include/instantee/<module>/`; native
implementation headers stay in the module's `src/` directory.

Preserve C++23 without extensions, checked inputs, explicit ownership and
`std::expected` for expected failures. Resource destructors only clean up;
fallible finalization is explicit. Review callback lifetimes and stop/wake/join
ordering whenever adding asynchronous work. Keep queues and pools bounded.

Keep changes focused, document public behavior and include tests for relevant
error paths. Do not create unused directories or speculative framework layers.
Update instructions when commands, paths or observable behavior change.

## Local data and commits

Keep the design pack and `implementation-evidence/` local and ignored.
Do not force-add them. Do not commit build output, raw reports, credentials,
personal paths, device identifiers or private configuration.

Review the full proposed contents, run an available secret scanner and inspect
the staged diff and commit metadata. Stage explicit delivery paths; preserve
pre-existing unrelated edits. Never rely on `.gitignore` or a scanner alone to
decide that content is safe. Use a non-personal project identity for commits.
Do not push unless explicitly requested.
