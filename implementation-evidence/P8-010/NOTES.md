# Bounded queue / backpressure load audit

Audits the existing runtime backpressure stack (no second queue
implementation): `FrameCreditPool` (K), `BoundedQueue`, `ReorderBuffer`,
`FrameEvalScheduler`, plus a separate E-limit pool standing in for the encoder
pixel-buffer threshold.

## Changes

- Added `BoundedQueue::try_push` returning `would_block` without moving the
  caller's value (contract non-blocking insert).
- Composed load audit: K=2, queue capacity 1, delayed oldest frame, slow sink
  gated by independent E=2; asserts plateaus, ordered delivery, and observed
  wait (not spin-only progress).

## Ownership / lifetime / concurrency

Credits stay with leases across failed push. Queue abort/close wake waiters;
workers share one scheduler `stop_source`. Encoder E credits are acquired only
after ordered `take_next`, so export never reorders under backpressure.

Measurement: none. Evidence tier B (queues/threads/stop; ASan/TSan).
