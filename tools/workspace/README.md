# Native feasibility workspace

From the repository root with the [verified toolchain](../toolchain/README.md):

```sh
cmake --workflow --preset release
cmake --workflow --preset asan-ubsan
cmake --workflow --preset tsan
python3 -m unittest discover -s tools/toolchain -p 'test_*.py' -v
build/release/spike-render --help
build/release/spike-render --smoke
build/release/spike-benchmark --smoke
```

Each workflow configures, builds and runs CTest. Release uses `RelWithDebInfo`;
diagnostic builds use `Debug` and separate directories. Instrumentation applies
to every project translation unit and executable link. ASan/UBSan and TSan are
not combined. Sanitizers do not check every internal operation of system Apple
libraries or prove GPU/encoder resource lifetimes. Python 3 is required for
subprocess smoke tests; the CLI runtime does not use it.

`spike-render` and `spike-benchmark` accept exactly one option: `--help` or
`--smoke`. Smoke checks a synchronous Core Foundation round trip and opens a
headless Metal device/queue through the pinned `metal-cpp` backend. It reports
real device capabilities as JSON, creates no files and returns
`published=false` with `certified=false`. The report uses explicit byte units
and does not include device registry identifiers. Unknown, duplicate or combined
arguments return exit code 2 and JSON on stderr. Diagnostics do not echo user
input. Without arguments, the renderer returns code 5 (`renderer_unavailable`)
and the benchmark returns code 2 (`benchmark_unavailable`). Rendering, export
and measurement await later implementation. A stdout write or flush failure
returns code 11 and minimal diagnostics on stderr.

Missing Metal, failure to create a command queue, or a default device without
Apple7 (M1 baseline) returns exit code 5 with a structured GPU diagnostic.
Retained Metal objects use a typed null-safe RAII owner that adopts +1 Create
results inside an autorelease pool; public headers expose only C++ value types.
`NS::SharedPtr` is avoided because its null destructor/assignment path is unsafe
under UndefinedBehaviorSanitizer.

The supported entry point for the future release benchmark is:

```sh
cmake --build --preset release --target run-spike-benchmark
```

The target first builds all required test programs and executables, then runs
CPU and smoke tests. Any test failure prevents the benchmark process from
starting. Only successful tests allow `spike-benchmark` to launch; it currently
returns an explicit unavailable error. The target therefore still returns a
nonzero result after a successful preflight. It is unavailable in sanitized or
unoptimized configurations. This is not a measurement harness; build and test
time must not enter the future export process's spawn-to-wait interval.

`engine/apps/cli` owns the parser and both spike executables. Shared error types
belong to the small header-only `engine/src/diagnostics` module with the public
prefix `instantee/diagnostics/`. The Apple adapter is a separate static library
in `engine/native/export-apple`. The Metal backend is `engine/src/render-metal`
with private access to `third_party/metal-cpp`. Public headers live only under
each module's `include/instantee/<module>/`; private wrappers and metal-cpp
remain out of the public include path. The adapters depend on diagnostics rather
than the CLI.

Every module, application and test group owns its `CMakeLists.txt`. `cmake/`
contains shared build rules and toolchain probes only;
`benchmarks/CMakeLists.txt` owns the preflight and benchmark entry point. A CPU
contract test compiles public headers without Apple frameworks. Build integration
tests inspect transitive includes and verify that private CF, Metal and CLI
headers are unavailable through the public include paths.

Private `CfOwner<Ref>` adopts only an already owned +1 Create/Copy reference and
releases it with `CFRelease`. Metal device and queue use `NS::TransferPtr` for
+1 Create results. Owners are move-only; moves and cleanup are `noexcept`.

Relevant contracts: [CMake workflow presets](https://cmake.org/cmake/help/v4.3/manual/cmake-presets.7.html),
[Core Foundation ownership](https://developer.apple.com/library/archive/documentation/CoreFoundation/Conceptual/CFMemoryMgmt/Concepts/Ownership.html)
and [metal-cpp memory management](https://github.com/apple/metal-cpp/blob/main/README.md).
