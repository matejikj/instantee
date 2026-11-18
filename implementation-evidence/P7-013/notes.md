# Puppet expressions

## Acceptance
- Three expressions of one character (`neutral`, `smile`, `surprise`) evaluate
  with identical stable face part ids (`eye_l`, `eye_r`, `mouth`, `head`).

## Design
- Animatable channels (`blink`, `mouth_open`, `head_turn`, `eye_widen`) map to
  part scale/opacity drivers and a head joint rotation.
- Named expressions are blended by non-negative weights (renormalized). Unknown
  expression ids and negative weights fail closed.
- Default paper-face library lives in `scene-ir` next to rig/FK/skin APIs.

## Measurement
- none
