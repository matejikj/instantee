# 2D IK solver

## Acceptance
- IK is deterministic for the same inputs, never yields NaN poses/errors, and
  uses a defined fallback when the target is outside max reach (project onto
  the reach circle, then solve).

## Design
- CCD over a contiguous parent chain (minimum two bone segments) built on
  the existing FK evaluator in `scene-ir`.
- Fixed iteration cap and pixel tolerance; optional hinge clamps from the
  puppet-rig document. No RNG and no GPU.
- Unreachable targets set `used_reach_projection` and
  `fallback_unreachable` once the projected tip is within tolerance (or
  `iteration_cap` with finite poses if still short).

## Measurement
- none
