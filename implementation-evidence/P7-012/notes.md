# Mesh deformation / skin bindings

## Acceptance
- Skinning matches reference deformation goldens within tolerance.
- Unsupported rig subsets (missing bindings, non-paper_2d_v1, influence
  budget overflow) fail with an explicit error rather than a soft fallback.

## Design
- CPU linear-blend skinning lives in `scene-ir` beside the puppet rig/FK APIs
  so geometry stays free of scene-ir coupling.
- Layer quads inherit documented `skin_bindings` influences (≤4). Skin matrices
  are `posed_world * inverse(bind_world)` and can feed a later cheap GPU path.
- Bind-identity and rigid single-bone cases are checked analytically; a text
  golden locks the shoulder-+90° forearm deform.

## Measurement
- none
