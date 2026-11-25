# Evidence: crash consistency and durability classes

## Acceptance

Before publish the target is absent or byte-identical to the previous file.
After publish only a valid payload remains with a truthful durability state.
SIGKILL is not treated as power-loss evidence. Unsupported durable-local
profiles are rejected instead of silently demoted.

## Measurement

`none` — correctness and capability evidence only; no M1 Air performance claim.

## Evidence tier

C — commands/tests, sanitizers, anonymized capability/crash matrix, decision
record. Durability work raises the floor above the backlog minimum of A.

## Artifacts

- `crash-matrix.json` — SIGKILL at finish/file-sync/rename/metadata-sync/report
- `capability-report.json` — local mount probe and durable-local gate
- `decision.md` — durability class decision; SIGKILL ≠ power-loss
- `ownership-note.md` — staging ownership and crash/fault boundaries
- `commands.txt` — reproduction
- `checks.json` — `tools/dev/check.py --asan --tsan`
