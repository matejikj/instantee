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

`release`, `toolchain`, `asan-ubsan` and `tsan` use the **certification**
toolchain lock (exact CMake/Ninja/AppleClang/Xcode/SDK versions and the Node
pin). For day-to-day native work without the Node lock, use the **development**
preset, which still requires Darwin/arm64/Ninja/AppleClang and the libc++
feature probes:

```sh
cmake --workflow --preset dev
```

Minimal versioned CI is [`.github/workflows/ci.yml`](.github/workflows/ci.yml)
and runs the development workflow, module-boundary check, `clang-format` and
`clang-tidy` (see [clang-tidy pin](tools/toolchain/CLANG_TIDY.md)). Certification
measurements stay on the reference machine with the locked presets.

The release workflow configures, builds and runs CTest. Both executables currently
support help and a smoke check that opens the Apple workspace plus a headless
Metal device and prints capability JSON. Actual rendering and measurement return
explicit unavailable errors. See the
[CLI/workspace guide](tools/workspace/README.md) for exit codes and the checked
benchmark entry point.

Code is divided by responsibility: [engine/](engine/README.md) owns the native
runtime, [authoring/](authoring/README.md) owns the TypeScript SDK, and
[contracts/](contracts/README.md) owns the shared data boundary. The authoring
package is currently a buildable shell without an implemented SDK API.

## Verification

Run the risk-selected checks for the current change with one command:

```sh
tools/dev/check.py --dry-run          # show the selected checks
tools/dev/check.py                    # run them
tools/dev/check.py --report PATH/checks.json
tools/dev/check.py --gate             # full matrix for a phase gate
```

The script diffs the working tree against `--base` (default `HEAD`) and
selects checks by risk:

| Change | Added checks |
|---|---|
| Any change | whitespace, tracker/backlog sync, `clang-format` on changed native sources, release build, CTest without the long `pts` label |
| Native translation units, once `.clang-tidy` exists | `clang-tidy` with warnings as errors |
| Native sources or build files | `asan-ubsan` workflow |
| Runtime, threads, queues, callbacks, stop/cancel | `tsan` workflow |
| Export, timing or core model | CTest including the `pts` label |
| Authoring or contracts | Node toolchain check, typecheck and build |
| A Python tool directory | That directory's unit tests |

Use `--asan`, `--tsan` or `--pts` to force a check that path inference cannot
see. Xcode does not ship `clang-tidy`; when it is installed outside `PATH`,
set `CLANG_TIDY` to its executable instead of putting a second compiler
toolchain on `PATH`. The report contains repository-relative commands, statuses, durations and
CTest counts only; it never stores raw output or absolute paths. A run with an
unavailable tool is `incomplete`, not `passed`, and must be reported as such.

`tools/dev/sync_tracker.py` checks that the tracker lists every backlog task
and delivery slice and records the reviewed backlog digest. It is skipped
without the local planning pack. After reviewing a regenerated backlog, update
the tracker entries and record the digest with `--write-digest`.

Measure performance only when the task asks for it: reference series for
benchmark scenes and gates, focused benchmarks only where a task names one.
Other changes are verified by correctness tests and make no performance claim.

Machine-profile tests that require the ignored local reference evidence are
explicitly skipped when it is unavailable. Collector regression tests still run.

### Evidence levels

| Level | Contents | Required for |
|---|---|---|
| A | Commands, tests and result plus `checks.json` | Every change |
| B | Level A plus sanitizer results and a short ownership, lifetime or concurrency note | Native resources, threads, queues, callbacks or stop/cancel |
| C | Reviewed package with anonymized metadata and a decision record; raw reports stay outside the repository | Phase gates, measurements, security and durability |

Tracker entries point to the evidence directory and its `checks.json` rather
than listing every file. Name individual artifacts only where an acceptance
criterion depends on them.

### Delivery slices

Where the backlog groups tasks into a delivery slice, deliver the slice as one
reviewable commit. Every task in it keeps its own acceptance, evidence and
status in the tracker. Without a slice, deliver one task per commit.

The presets use separate build directories. Sanitized runs are correctness
diagnostics and must not be presented as release performance measurements.
System Apple frameworks are not fully instrumented by the project's sanitizers.
When only rerunning an existing configuration, use `cmake --build --preset NAME`
and `ctest --preset NAME`.

Release-gate CI (`.github/workflows/ci.yml`) is stricter than the P1 smoke path:
full-own-code clang-tidy, development ASan+UBSan and TSan workflows, contracts
and authoring checks, plus dependency/API policy jobs. Required check names,
dev-versus-certification preset rules and native coverage limits are documented
in [tools/toolchain/CI_RELEASE_GATE.md](tools/toolchain/CI_RELEASE_GATE.md).
Own targets already compile and link with `-Werror` through
`instantee_build_options`.

Format changed C++/Objective-C++ headers and sources using `xcrun clang-format`;
verify them with `--dry-run --Werror`. Use Clang static analysis or the project's
available lint tooling and report unavailable checks. Run relevant behavioral
tests before committing. Moving files or changing CMake include paths requires
build integration and public-header boundary checks as well as runtime tests.

The ignored planning pack has additional contract checks for contributors who
have it locally. It is not required to configure, build or test this workspace.

## Adding code

Follow [the repository structure and dependency rules](ARCHITECTURE.md) and the
[implementation architecture guides](architecture/README.md) (module
owners, data flow, style extension without GPU handles, ADR index). Keep the
root build short and define targets in their owning directory. Use
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

Use English for all versioned descriptions, Markdown documentation, code
comments, docstrings, diagnostics and commit messages. Review prose for language
as part of the staged-content review. Non-English text is appropriate only as
intentional test or input data, not as documentation or commentary.

## Local data and commits

Keep the design pack and `implementation-evidence/` local and ignored.
Do not force-add them. Do not commit build output, raw reports, credentials,
personal paths, device identifiers or private configuration.

Review the full proposed contents, run an available secret scanner and inspect
the staged diff and commit metadata. Stage explicit delivery paths; preserve
pre-existing unrelated edits. Never rely on `.gitignore` or a scanner alone to
decide that content is safe. Commit under your own Git identity. Commit
messages carry no tool or agent attribution; enable the repository hook that
removes such trailers:

```sh
git config core.hooksPath tools/dev/hooks
```

Do not push unless explicitly requested.
