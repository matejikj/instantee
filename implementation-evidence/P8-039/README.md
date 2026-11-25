# Evidence: backpressure and shutdown without deadlock

## Acceptance

K/E and memory plateaus hold under controlled load; no dropped export frames,
spin waits, or deadlocks; every waiting path can be terminated (stop, close,
abort, or native deadline).

## Measurement

`none` — overload report is a correctness plateau artifact, not certified M1 Air
performance.

## Evidence tier

B — commands/tests, ASan/UBSan + TSan, ownership/lifetime/concurrency note.

## Artifacts

- `overload-report.json` — K/E/queue/memory plateau + wait metrics (cpu-fake)
- `concurrency-note.md` — lifetime/concurrency summary
- `commands.txt` — reproduction
- `checks.json` — `tools/dev/check.py --asan --tsan`
