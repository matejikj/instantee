# Evidence: V1.0.0 Git tag and release package

## Acceptance

Git tag `v1.0.0` matches the tested build and macOS package checksums.

## Measurement

`none` — packaging, smoke install, and gate matrix only. No new B-0x series.

## Evidence tier

C — gate decision, package digests, install smoke, full `check.py --gate`.

## Artifacts

- `gate-report.json` — package digests, smoke, dossier/task gates
- `tag-verification.json` — annotated tag points at the release commit
- `decision.md` — P8 phase gate PASS
- `commands.txt` — reproduction
- `checks.json` — `tools/dev/check.py --gate`
