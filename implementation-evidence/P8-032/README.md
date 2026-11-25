# Troubleshooting and benchmark playbooks

## Acceptance

Každé dokumentované běžné selhání má reprodukční postup a diagnostické informace.

## Decisions

- Versioned `playbooks/troubleshooting.md` covers export failure, fonts, color,
  GPU device loss, memory pressure and invalid measurement with Symptoms /
  Reproduction / Diagnostics / Next steps on every case.
- `playbooks/benchmark.md` separates smoke/tutorial runs from reference-series
  certification and points at existing harness docs.
- `tools/dev/check_playbooks.py` dry-runs structural coverage (required cases and
  sections); sample CLI reproduction is anonymized in evidence.

## Verification

- `python3 tools/dev/check_playbooks.py` result: `passed`.
- `python3 -m unittest discover -s tools/dev -p 'test_playbooks.py' -v`
  result: `passed`.
- Sample reproduction in `playbook-dry-run.txt` (exit 2, anonymized stderr).
- `tools/dev/check.py --keep-going --report implementation-evidence/P8-032/checks.json`
  (see checks.json).
- Measurement: none. No performance certification claim.

## Evidence tier

A (commands, tests, `checks.json`, anonymized dry-run transcript).
