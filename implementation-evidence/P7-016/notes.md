# Puppet secondary motion

## Acceptance
- Absolute-frame evaluation of blink / breath / sway / ear / cloth is
  random-access: reordering frames yields identical poses.

## Design
- Pure `f(seed, node_id, frame)` using the shared SplitMix64/FNV channel hash.
  Blink pulse start is hashed per cycle; breath/sway/ear/cloth are analytic
  sines with seed-stable phase offsets plus tiny per-frame jitter.
- Value types only; no shared mutable state. Composition accumulates joint
  deltas and appends ear/cloth/torso part drivers onto expression poses.

## Measurement
- none

## Sanitizers
- ASan/UBSan workflow passed as selected by change-risk checks.
