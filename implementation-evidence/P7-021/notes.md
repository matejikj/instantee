# Paper Cinema lighting and shot templates

## Acceptance
- Templates use the shared render graph and do not need style-name conditions
  in core/render-core. An automatic source audit rejects style-name branches.

## Design
- Direction owns five curated shot templates (reveal stage, object closeup,
  puppet dialogue, technical explanation, atmospheric dissolve). Each selects
  a camera preset, semantic intent, and a lighting *feature* id.
- Render-core owns lighting recipes and builds a shared two-pass graph
  (shadow buffer → main color, optional veil). Recipe keys are capability ids,
  never Style Bible package names.
- Atmospheric dissolve lowers to TransitionKind::reveal (Scene IR dissolve).

## Ownership / lifetime
- Sequence assembly and graph descriptors are value types; no shared mutable
  generator state.

## Measurement
- none

## Sanitizers
- ASan/UBSan workflow selected by change-risk checks.
