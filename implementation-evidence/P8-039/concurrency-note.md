# Ownership / lifetime / concurrency note

## Controlled load

`FrameEvalScheduler` with queue capacity 1, K∈{2,3}, independent encoder E=2,
delayed oldest frame, and a slow ordered sink. Credits stay with leases;
failed/cancelled pushes do not drop export frames. Peak K/E and a synthetic
accounted-byte plateau are asserted.

## Waiting paths

- Full push / empty pop terminate via `stop_token` cancel.
- Native `deadline_exceeded` terminates blocked push/pop/credit acquire.
- `close()` drains remaining items in order; `abort()` wakes waiters and retires
  pending ownership outside the mutex.
- Notify storms (repeated abort/close while waiters block) exercise predicate
  loops that tolerate spurious wakeups without hang or double-free.

## Sanitizers

CPU fake backend only in this audit (no Metal). TSan covers queues, credits,
scheduler workers, and stop/cancel. Measurement: none.
