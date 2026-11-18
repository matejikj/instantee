# Puppet walk cycle

## Acceptance
- Timing/pose reference fixtures and foot-slide tolerance are met for a
  distance-driven biped plan; subjective smoothness alone is not enough —
  a signed checklist review record accompanies the goldens.

## Design
- Absolute-frame evaluation (start / cycle / stop / idle) with analytic plant
  anchors so planted-foot world X is a pure function of the plan (no prior
  frame state).
- After relative gait FK, a root X correction locks the planted foot to its
  plant mark; slide is measured across consecutive cruise frames.
- Value types only; no shared mutable state or async lifetimes. Skin bindings
  on the biped rig remain composable with the existing LBS path.

## Measurement
- none

## Sanitizers
- ASan/UBSan workflow passed as selected by change-risk checks.
