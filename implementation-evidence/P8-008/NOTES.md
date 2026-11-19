# Cancellation fault matrix audit

Production cancel paths already existed (shared `stop_source`/`stop_token`,
bounded-queue abort/wake, FrameEvalScheduler abort, RenderJob publish gate,
export sink cancel, process supervisor). This delivery proves the state matrix
and closes one orchestration gap: when stop wins at `enter(publishing)`, the
export path now cancels the sink so native waiters cannot outlive the job.

## Ownership / lifetime / concurrency

- `RenderJob` remains sole scene owner; cancel after publish never clears
  published* phases or deletes the target.
- Export sinks may spawn a deferred cancel-completion on a `shared_ptr`
  context that outlives the sink object; the sink destructor joins that
  thread before returning.
- Queue/scheduler abort wakes blocked producers/consumers under the queue's
  own mutexes; stop callbacks must not join the signal thread (existing
  StopSignals contract).
- Hung native APIs stay bounded by the process supervisor (SIGTERM then
  SIGKILL of the child), not by tearing down live C++ threads.

Measurement: none. Evidence tier B (stop/cancel + sanitizers).
