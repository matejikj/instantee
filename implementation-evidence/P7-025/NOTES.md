# Paper Cinema phase gate

## Acceptance
A fully animated paper cinema demo exists with green B-03, art, stability, and
linter gates. B-03 cold wall >360 s or a broken reference puppet/paper scene
FAIL.

## Deliverables
- Component inventory and known limits (including declared 2D puppet scope) in
  `gate-report.json`.
- Design/performance review summary in the same report.
- Referenced B-03 reference series (3 cold + 3 warm) with ADR-0014
  `paper-b03-v1` thresholds; run reports stay `certified=false`.
- Named ctest probes: B-03 demo, visual goldens, style review, cinematic
  templates, puppet pack/FK, paper fold, layout lint, lint policy.
- Full `tools/dev/check.py --gate` matrix.

## Ownership / lifetime / concurrency
- Gate assembler is synchronous Python over tracker JSON, baseline JSON, and
  ctest probes. Export child ownership remains as in the B-03 harness
  (RenderJob, preview RGBA, AppleVideoSink E=8, transactional publish).
- No new queues/threads for the gate itself.

## Decision
See `gate-decision.json`. PASS unlocks P8 release hardening.
