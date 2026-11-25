# Evidence: ownership, RAII, and async lifetime audit

## Acceptance

No premature release/reuse, double-delete, or callback into a destroyed job.
Every exclusive resource declares owner / borrow / completion / limit / stop
behaviour with fault evidence.

## Measurement

`none` — sanitizer and stress runs are not certified M1 Air performance.

## Evidence tier

B — commands/tests, ASan/UBSan + TSan, and ownership/lifetime note.

## Artifacts

- `ownership-audit-check.json` — inventory checker
- `ownership-lifetime-note.md` — ownership/lifetime/concurrency note
- `commands.txt` — reproduction
- `checks.json` — `tools/dev/check.py --asan --tsan`
