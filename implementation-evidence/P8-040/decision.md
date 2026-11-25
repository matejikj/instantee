# Decision: crash visibility vs durable-local

## Decision

1. `process_crash_visible` is the class proven by SIGKILL / process-death tests
   around atomic rename. Pre-rename kills leave the previous target (or none);
   post-rename kills leave a complete published file.
2. `durable_local` additionally requires APFS or HFS+ with `F_FULLFSYNC` on the
   output parent. Unsupported mounts and injected unsupported profiles fail
   begin with `durability_unsupported` — no silent fallback to `fsync`.
3. SIGKILL, RAII cleanup, and rename atomicity are **not** power-loss or bad-
   media certification. Absolute disk durability remains out of V1 scope.

## Storage fault experiment (controlled)

Unit faults already exercise short-write / EINTR retry, ENOSPC, EIO, pre-publish
file-sync failure (target unchanged), and post-publish directory-sync failure
(`published=true`, durability `unknown`). Live durable-local publish on the
probed APFS volume uses `F_FULLFSYNC` before rename.

## Result

Accept process-crash visibility for local V1 volumes. Accept durable-local only
where the capability probe reports support. Record SIGKILL evidence with
`sigkill_is_power_loss_evidence: false`.
