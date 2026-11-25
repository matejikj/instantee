# API docs and three end-to-end tutorials

## Acceptance

Čistá instalace podle tutoriálu vytvoří všechna tři videa.

## Decisions

- Document the SDK + CLI surface in `authoring/API.md` and keep the frozen
  TypeScript contract in `PUBLIC_API.md`.
- Ship three public-SDK generators under `authoring/examples/tutorials/` plus
  guides in `tutorials/` (Editorial, Notebook, Paper Cinema).
- Executable docs test `tools/dev/run_tutorial_e2e.py` compiles each sample
  with its Style Bible and renders three MP4s. Short `--frames` is for smoke
  only; reports set `certified_performance: false`.

## Verification

- `python3 tools/dev/run_tutorial_e2e.py --cli build/release/instantee --work-dir <tmp> --frames 30 --write-report implementation-evidence/P8-031/tutorial-artifacts/tutorial-e2e-report.json`
  result: `passed` (editorial, notebook, paper-cinema).
- `python3 -m unittest discover -s tools/dev -p 'test_tutorial_e2e.py' -v`
  result: `passed`.
- `tools/dev/check.py --keep-going --report implementation-evidence/P8-031/checks.json`
  (see checks.json).
- Measurement: none. No performance certification claim.

## Evidence tier

A (commands, tests, `checks.json`, anonymized tutorial e2e report).
