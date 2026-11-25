# Evidence: metric reproducibility audit

## Acceptance

Raw evidence matches reports; units, intervals, and profiles stay explicit.
Design fixtures, lost samples, and missing metrics cannot receive PASS.

## Measurement

`none` — independent recompute of committed 3× cold/warm series summaries and
synthetic oracles only. No new M1 Air performance claim.

## Evidence tier

C — commands/tests, sanitizers where risk-selected, anonymized audit report,
decision record. Raised above backlog minimum A because the subject is
measurement reproducibility.

## Artifacts

- `audit-report.json` — oracle, completeness, PASS gates, series recompute, overhead
- `decision.md` — audit decision
- `ownership-note.md` — sample ownership / invalidation rules
- `commands.txt` — reproduction
- `checks.json` — `tools/dev/check.py`
