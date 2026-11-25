# Concurrency / backpressure audits

Shutdown and overload proofs for the CPU evaluation pipeline live in:

- `engine/tests/unit/runtime/backpressure_shutdown_audit_test.cpp`
- `engine/tests/unit/runtime/backpressure_load_audit_test.cpp`
- `engine/tests/unit/runtime/backpressure_test.cpp`

The shutdown audit covers queue capacity 1, K=2/3, slow sink, delayed oldest
frame, full/empty stop, close/drain/abort, native deadlines, and notify-storm
predicate safety on the fake CPU backend. Run with TSan via
`tools/dev/check.py --tsan`.

Overload reports emitted by the shutdown audit set
`certified_performance: false` and are not M1 Air reference-series evidence.
