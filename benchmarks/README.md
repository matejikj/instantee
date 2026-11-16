# Benchmark entry point

This directory owns the optimized, unsanitized `run-spike-benchmark` target.
It builds all required test programs and executables, then runs the CPU and
smoke tests before starting the benchmark process. Failed tests prevent launch.

There are currently no measured workloads or benchmark results. The process
returns an explicit unavailable error after a successful preflight. Future
fixtures and profiles belong here; local run reports belong in the ignored
`implementation-evidence/` directory.

See the [workspace guide](../tools/workspace/README.md) for commands and limits.
