# Procedural background kit

## Acceptance
- A seed produces stable scenery (mountains, clouds, buildings, trees,
  atmosphere) and the total asset count stays within explicit limits.

## Design
- Pure `f(seed, limits)` generation over a depth-stage stack (10–20 planes).
- Clouds/buildings instantiate existing paper prop blueprints; mountains,
  trees, and atmosphere use lightweight silhouette cards (no new prop kinds).
- Per-kind and total caps fail closed when params are invalid.

## Ownership / lifetime
- Scenery is a value type; no shared mutable generator state.

## Measurement
- none

## Sanitizers
- ASan/UBSan workflow passed as selected by change-risk checks.
