# Render job state machine audit

Production `RenderJob` phases already existed from the pipeline promotion work.
This delivery audits them against the publication contract and closes gaps:

- Sticky `first_error` latch for first-fault propagation.
- Legal `published → published_durability_unknown` transition.
- Durability-unknown outranks side-report failure when both are observed after
  rename (metadata sync faults).
- Test-only `OutputTransactionFaults` plumbing on scene video export.
- Fault tests: pre-publish preserves the old target; post-publish acknowledges
  the new MP4 with truthful durability; late cancel cannot pretend absence.

## Ownership / lifetime / concurrency

`RenderJob` remains sole owner of `unique_ptr<const CompiledScene>`. Phase
mutations and the first-error latch are single-owner (serial export/orchestrator
thread). `stop_token` is observed only at `enter()` gates before Publishing;
callbacks are not introduced. OutputTransaction still owns staging and never
deletes a published target from its destructor.

Measurement: none. Evidence tier B (native + stop/cancel paths; ASan/TSan).
