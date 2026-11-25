# Quality lint and accessibility release policy

## Acceptance

Žádný releasovaný example neobsahuje neodsouhlasené chyby.

## Decisions

- Formalize blocking versus warning lint codes in
  `contracts/quality-release/policy-v1.json` and lock empty findings snapshots
  for the three Style Bible films plus the three-styles authoring example.
- Reuse approved visual-review checklist ids for phone/safe-area legibility on
  style films; require usable `tokens.safe_area` insets on Style Bible packages
  for the authoring example.
- Hosted job `quality-lint` publishes an anonymized report with
  `certified_performance: false`. Unwaived errors or unused waivers fail the
  gate.

## Verification

- `python3 tools/dev/check_quality_release.py --cli build/release/instantee --write-report implementation-evidence/P8-028/quality-lint-artifacts/quality-lint-report.json`
  result: `passed`.
- `python3 -m unittest discover -s tools/dev -p 'test_quality_release.py' -v`
  result: `passed`.
- `tools/dev/check.py --keep-going --report implementation-evidence/P8-028/checks.json`
  (see checks.json).
- Measurement: none. No performance certification claim.

## Evidence tier

A (commands, tests, `checks.json`, anonymized CI report artifact).
