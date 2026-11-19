# Scene complexity budget enforcement

Tightens the P3 draft complexity tier with explicit structural caps
(nodes/depth/assets/glyphs/masks/layers/keyframes/canvas). Measurement is
count-based (not RSS). Exceeding a cap returns `resource_budget_exceeded`
before large runtime allocations.

Compile defaults to `p3-foundation-draft`; empty `complexity_tier_id` skips.
Style reel ids inherit the same caps. ADR-0014 documents the numeric table.

Measurement: none. Evidence tier B (A + ASan + ownership note).
Unrelated intermittent ASan flake in frame_eval_scheduler recorded separately;
re-run of the full asan-ubsan matrix passed.
