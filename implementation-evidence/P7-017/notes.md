# Paper fold / unfold

## Acceptance
- A letter-card mesh folds and unfolds from keyframed crease angles without a
  3D physics simulator; goldens lock flat, half, and closed poses.

## Design
- Crease lines + exclusive vertex segments; hinged panels rotate with Rodrigues
  about the crease axis in bind space (0 = flat, π = stacked).
- Local linear keyframes (no runtime track dependency — render-core stays on
  diagnostics/core-model only). Draw order is back-to-front by posed depth,
  then occlusion_order.
- Value types only; no shared mutable state or async lifetimes.

## Measurement
- none

## Sanitizers
- ASan/UBSan workflow passed as selected by change-risk checks.
