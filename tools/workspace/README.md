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
`--smoke`. Smoke checks a synchronous Core Foundation round trip inside an
autorelease pool, creates no files and returns JSON with `published=false` and
`certified=false`. Unknown, duplicate or combined arguments return exit code 2
and JSON on stderr. Diagnostics do not echo user input. Without arguments,
the renderer returns code 5 (`renderer_unavailable`) and the benchmark returns
code 2 (`benchmark_unavailable`). Rendering, export and measurement await later
implementation. A stdout write or flush failure returns code 11 and minimal
diagnostics on stderr.

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
in `engine/native/export-apple`, compiled as Objective-C++ with ARC. Its public
headers live only in `include/instantee/export_apple/`; the private CF wrapper
and implementation live in `src/`. The adapter depends on diagnostics rather
than the CLI. It has no persistent state requiring PImpl.

Every module, application and test group owns its `CMakeLists.txt`. `cmake/`
contains shared build rules and toolchain probes only;
`benchmarks/CMakeLists.txt` owns the preflight and benchmark entry point. A CPU
contract test compiles public headers without Apple frameworks. Build integration
tests inspect transitive includes and verify that consumers cannot include
private CF or CLI headers through the public include paths.

Private `CfOwner<Ref>` adopts only an already owned +1 Create/Copy reference and
releases it with `CFRelease`. `get()` borrows the reference; adopting a +0 Get
reference requires a prior `CFRetain`. Owners are move-only; moves and cleanup
are `noexcept`. ARC manages Objective-C objects, not owned CF references. Native
tests verify an independent copy of input bytes, ownership transfer and an
explicitly retained reference surviving retirement of the original owner.

Relevant contracts: [CMake workflow presets](https://cmake.org/cmake/help/v4.3/manual/cmake-presets.7.html)
and [Core Foundation ownership](https://developer.apple.com/library/archive/documentation/CoreFoundation/Conceptual/CFMemoryMgmt/Concepts/Ownership.html).
