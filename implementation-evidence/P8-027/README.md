# Visual regression baseline for three styles

## Acceptance

An unreviewed pixel change or loss of readability fails the release gate.

## Decisions

- Bind the three Style Bible golden trees in
  `contracts/visual-baseline/registry-v1.json` to the Apple Silicon macOS Metal
  family and shared exact pixel tolerances.
- Enforce approval through existing `CHANGE_RECORD.json` + approved
  `visual-review/review-record.json`; readability checklist ids from the
  registry must stay passed.
- Add hosted job `visual-regression` that runs the baseline/approval check and
  the three visual-acceptance CTests, uploading anonymized artifacts with
  `certified_performance: false`.

## Verification

- `python3 tools/dev/run_visual_regression.py -p build/release --artifact-dir implementation-evidence/P8-027/visual-artifacts --enforce-platform`
  result: `passed`.
- `tools/dev/check.py --keep-going --report implementation-evidence/P8-027/checks.json`
  result: `passed` (release without PTS label, ASan+UBSan, authoring/contracts,
  tools/dev and tools/toolchain). TSan was not selected by path risk.
- Measurement: none. No performance certification claim.

## Evidence tier

A (commands, tests, `checks.json`, anonymized CI artifacts).
