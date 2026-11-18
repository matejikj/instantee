# Paper layered shadow separation

## Acceptance
- Inter-layer offset drops and contact cues make depth readable without ray
  tracing.
- Soft blur is quality-tier clamped and clipped to the receiving sheet / stage
  floor so soft edges cannot form a floating halo outside the receiver.

## Design
- `paper_layered_shadow` in render-core builds on existing `ShadowQualityTier`
  blur limits and the `paper_layered_depth_probe` capability hook.
- A simple nearest-layer depth map decides receivers; compositing paints
  far→near (sheet, then nearer casters darkening that sheet) plus stage-floor
  drops on uncovered background.
- No style-package branch and no Metal types in the CPU golden path.

## Measurement
- none
