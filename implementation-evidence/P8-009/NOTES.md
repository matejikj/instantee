# Controlled export error recovery

Adds structured inspection/recovery for export workspaces:

- `inspect_export_workspace` classifies published targets, missing reports,
  orphan staging, active staging and foreign entries.
- `recover_orphan_staging` deletes only orphan `.instantee-txn-*` directories
  whose ownership marker matches the nonce and whose exclusive flock is free.
- Live `OutputTransaction` jobs hold `flock(LOCK_EX)` on the marker for their
  lifetime so recovery cannot remove an active job's staging.
- Published MP4/report paths and foreign directories are never deleted.
- Inspection/recovery APIs are `noexcept` and return `std::expected` so
  expected failures do not escape as exceptions.

## Ownership / lifetime

Marker lock fd is owned by `OutputTransaction::State`, unlocked before staging
removal on publish or destructor cleanup. Recovery re-probes the lock
immediately before `remove_all`.

Measurement: none. Evidence tier B (native filesystem + locks; ASan).
