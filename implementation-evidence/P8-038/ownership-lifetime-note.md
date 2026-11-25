# Ownership / lifetime / concurrency note

## Owners

- `GpuResourceRegistry` destroys type-erased payloads exactly once when holds
  drain; `SubmissionContext` is the hold owner for a submission.
- `StagingLease` owns a fixed pool slot; GPU/sink holds keep frozen bytes
  immutable through cancel until delayed completion recycles the slot.
- `FrameCredit` returns admission capacity on release/destruction; pool
  capacity (K) is never exceeded by fallback allocation.
- `BoundedQueue::abort` extracts pending values and destroys them outside the
  mutex so waiters wake without double-free of queue storage.
- `RenderJob` owns the compiled scene; late completion uses `shared_ptr`
  contexts that outlive the job stack frame.
- Apple CF/ObjC owners use move-only retained wrappers inside an autorelease
  pool; public headers stay free of Apple types.

## Borrow

Generational handles and staging pixel spans are invalid after recycle/stale
generation. Queues move values — they never queue producer stack views.

## Stop / cancel

Cancel before publish is allowed on `RenderJob`. Submission cancel and staging
cancel do not clear busy bytes or double-destroy payloads. Queue abort retires
pending ownership once.

Measurement: none. Sanitizer evidence is correctness-only.
