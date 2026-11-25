# Evidence: publishable reference films

## Acceptance

Three silent 1080×1920 / 30 fps MP4s match the three Style Bibles; creative
signoff covers visual direction, tempo, transitions, and phone text readability.

## Measurement

`none` — export wall times are not a certified performance claim.

## Evidence tier

A — commands, tests, export/probe report, signoff, and `checks.json`.

## Artifacts

- `export-report.json` — allowlisted technical probes (no personal paths)
- `reference-films-check.json` — checker with `--require-files`
- `commands.txt` — reproduction
- `checks.json` — `tools/dev/check.py` report

MP4 binaries live under `build/dist/reference-films/` (not committed); digests
are locked in `contracts/release/reference-films/catalog-v1.json`.
