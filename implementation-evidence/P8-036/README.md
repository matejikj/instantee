# Evidence: V1 release certification dossier

## Acceptance

Every MUST checklist gate has real evidence and PASS. Ownership, concurrency,
crash/durability, and measurement audits are green. No blocking defect or
unexplained missing metric remains for the product dossier.

## Measurement

`none` — aggregates committed B-0x / audit / security / docs evidence. Does not
re-run reference series or claim new M1 Air timings.

## Evidence tier

C — signed checklist, anonymized dossier decision, known limitations, and
`checks.json`. Raised above backlog minimum A because this is the release
certification package.

## Artifacts

- `dossier.json` — aggregated FR/NFR, B-01..B-05, audits, gates
- `checklist.json` — signed V1.0 release checklist
- `decision.md` — accept/reject decision
- `commands.txt` — reproduction
- `checks.json` — `tools/dev/check.py`
