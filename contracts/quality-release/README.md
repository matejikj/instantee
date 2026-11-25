# Quality lint and accessibility release policy

Released Style Bible films and the verified three-styles authoring example must
pass a strict lint gate and a mobile/safe-area legibility check before merge.

## Policy

`policy-v1.json` names:

- **blocking_codes** — lint codes that may reject release when severity is
  `error` and no matching waiver exists (layout, typography, motion/palette).
- **warning_codes** — codes that may appear as non-blocking `warning`/`info` on
  a released film (for example soft cut density or text overflow hints).
- **released_examples** — locked findings snapshots plus legibility sources.

Waivers use the existing `lint-waivers/1` shape (`code`, optional `path` /
`element_id`, `reason`, `approval_id`). An unused waiver fails the gate.

## Legibility

- Style films reuse approved `visual-review/review-record.json` checklist ids
  (safe area, phone legibility, typography, reference frames).
- The three-styles authoring example requires each Style Bible package to
  declare a usable `tokens.safe_area` token (positive insets).

## Local check

```sh
python3 tools/dev/check_quality_release.py --write-report /tmp/quality-lint-report.json
```

Optional cross-check against the CLI evaluator (after a release/dev build):

```sh
python3 tools/dev/check_quality_release.py --cli build/release/instantee
```

Hosted CI job `quality-lint` uploads the anonymized report artifact. This gate
does not certify throughput, p99, CPU or memory.
