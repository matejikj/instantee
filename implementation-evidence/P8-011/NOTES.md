# Memory / resource budget pressure audit

Tightens budget enforcement already introduced with render jobs and target
pooling: overflow-safe RGBA8 estimates, remaining-byte preflight on the job,
and safe free-list eviction under pooled-byte pressure.

## Behaviour

- Exceeding `max_accounted_bytes` or pooled byte ceilings returns
  `resource_budget_exceeded` without resizing targets or lowering quality.
- Free-list eviction only retires unused residents; in-use leases are never
  reclaimed. When live usage still exceeds the ceiling, acquisition rejects.
- Glyph atlas LRU eviction (pinned-safe) remains the text cache pressure path.

Measurement: none. Evidence tier B (native resources / ownership).
