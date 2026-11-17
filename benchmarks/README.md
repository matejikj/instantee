# Benchmark entry point

This directory owns the optimized, unsanitized `run-spike-benchmark` target.
It builds all required test programs and executables, then runs the CPU and
smoke tests before starting the benchmark process. Failed tests prevent launch.

CPU micrometrics for frame evaluation and the bounded scheduler live in
`frame-eval-bench` (Google Benchmark pin `third_party/google-benchmark`,
packed by `tools/benchmark/frame_eval_bench.py`). Instance-batch planning and
CPU rasterize micrometrics live in `instance-batch-bench` (packed by
`tools/benchmark/instance_batch_bench.py`). Those reports are non-certified
development baselines; local JSON belongs under ignored
`implementation-evidence/`.

The legacy `spike-benchmark` process still returns an explicit unavailable
error for end-to-end export measurement. See the
[workspace guide](../tools/workspace/README.md) for commands and limits.
