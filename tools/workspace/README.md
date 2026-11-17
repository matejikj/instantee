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

The Metal backend can also render a deterministic 1080×1920 RGBA8 test pattern
with non-opaque alpha for frames 0 and 1. The spike compiles the MSL source once
at renderer creation from an offline-pinned metallib, draws to a private
offscreen texture, then synchronously blits into a shared staging buffer for CPU
verification. Native tests compare every pixel to a CPU oracle, require distinct
frame checksums, and write reference PNGs plus SHA-256 digests.

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

## Interruption and bounded shutdown

Both executables install `runtime::StopSignals` on the main thread before opening
native frameworks. SIGINT/SIGTERM are blocked before workers inherit the mask;
a joining `sigwait` thread requests the common stop source without entering
Apple APIs. One bridge may exist at a time. It must be destroyed on its creating
thread after all job workers have joined. Stop callbacks must be bounded and
must not join the signal thread or call a potentially blocking native API.
The current CLI still exposes only workspace smoke checks; export composition
is exercised by the native interruption test child.

Queue `close()` permits normal draining. `abort()` rejects pending work and
releases queued ownership outside the mutex; the coordinator requests stop
outside resource locks, closes admission, aborts queues and joins consumers
before destroying dependencies. Failed `push(std::move(lease))` preserves the
caller's lease. A stopped consumer receives no new queued work. Already submitted
GPU work stays owned until its completion; cancelling readback only refuses
pixel delivery. The export owner cancels the writer. `finish(stop)` waits for
its native completion acknowledgement even after stop. Never detach a blocked
thread or free resources because a local timer expired.

Use the process supervisor to bound a stuck native call, including GPU retirement,
writer cancellation/finalization, or media inspection:

```sh
python3 tools/workspace/supervise.py --timeout 120 --grace 10 -- \
  build/release/spike-render --smoke
```

Timeouts are positive finite seconds and are explicit run-profile settings, not
performance thresholds. The supervisor starts a separate child process group,
forwards SIGINT/SIGTERM, allows the configured grace interval, then sends SIGKILL
to the group and reaps the child. A watchdog expiry returns code 12 even if the
child subsequently exits cleanly; a forced kill reports `published=null` and
`durability=unknown`. The supervisor cannot infer whether a killed process
already renamed its output. Child stdout/stderr are inherited; collect raw
logs outside the repository and sanitize evidence before sharing it.

`OutputTransaction::commit(stop)` checks cancellation before file sync and at
the serial owner's entry into Publishing, immediately before rename. After that
gate, it completes publication and metadata sync and returns the actual result.
Late stop never removes a published output. Before publication, cancellation
preserves the previous target (or its absence); normal stack cleanup removes
only the private staging directory. SIGKILL may leave that job's private staging
artifacts. Do not delete arbitrary temporary directories or assume that a killed
writer can resume. Process-kill checks establish atomic visibility only; they
provide no power-loss durability certification.

The subprocess cancellation matrix covers both signals, absent/existing targets,
full/empty queues, exhausted credits and pixel pools, delayed completion, GPU
retirement, writer cancellation/finish, and stop after publication. Separate
SIGKILL cases check target visibility and staging permissions/markers. A fake
hung native call verifies watchdog escalation. Run these through CTest on a host
with Metal and hardware-encoder access; sanitizer runs are diagnostics.

The separate `spike-export-metrics` executable and [measurement harness](../benchmark/README.md)
now exercise test-pattern export with raw frame/resource samples, external
spawn-to-wait4 timing, and telemetry on/off trials. They do not implement the
scene benchmark CLI or certify a baseline.
