# Requirements coverage matrix

Machine-readable mapping of SAS §2 FR/NFR identifiers to backlog tasks,
verification strings, tracker evidence, and phase-gate tracking.

## Authoritative artifact
- `requirements-coverage-v1.json` — produced by
  `tools/dev/requirements_coverage.py`.

## Acceptance
- Every SAS MUST requirement is mapped to known backlog task IDs.
- Every release gate `P0-020`…`P8-037` is tracked under
  `IMPLEMENTATION_STATUS.json` → `phase_gates`.
- SAS and design-pack RTM identifier sets stay aligned.

## Refresh
```bash
python3 tools/dev/requirements_coverage.py --write --check
```
