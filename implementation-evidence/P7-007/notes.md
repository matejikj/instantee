# Cinematic camera presets

## Acceptance
- Dolly, truck, slow zoom, crane-like pan and rack-depth presets emit declared
  camera-capability tracks only (fail closed when required capability missing).
- On the same depth-stage scene, eased sampling keeps the subject framed with
  no direction-reversal jitter.

## Design
- Lives in `direction` atop camera language + Motion Grammar `camera_move`
  timing; uses registered TRS properties and core-model depth-stage projection
  for the demo sampler (no GPU / style forks).
- Two keyframes per property; optional grammar scales intensity and action
  window. Rack-depth approximates focus pull with a small axial shift under the
  focus capability (true optical DOF remains out of scope).

## Measurement
- none
