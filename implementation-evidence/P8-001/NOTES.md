# Requirements traceability review

## Acceptance
No MUST requirement is uncovered and no release gate is untracked.

## What changed
- `tools/dev/requirements_coverage.py` builds a live coverage matrix from SAS §2,
  the design-pack RTM, the backlog, and `IMPLEMENTATION_STATUS.json`.
- Committed matrix: `contracts/traceability/requirements-coverage-v1.json`.
- Design-pack RTM documents the live validator and matrix path; MANIFEST refreshed.
- Unit tests cover priority parsing, RTM extraction, and accept/reject gates.

## Result
- 35 requirements (33 MUST, 1 SHOULD, 1 STRETCH) all mapped.
- 9 release gates tracked (`P0`…`P8`); P0–P7 decided, P8 tracked pending.
- measurement=none; evidence_tier_min=A.

## Verification
- `python3 -m unittest discover -s tools/dev -p 'test_requirements_coverage.py'`
- `python3 tools/dev/requirements_coverage.py --check`
- `tools/dev/check.py --report implementation-evidence/P8-001/checks.json`
