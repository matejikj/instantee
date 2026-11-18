# Puppet rig interchange schema

## Acceptance
- Invalid puppet rig documents are rejected by schema and CPU semantic
  validation without any GPU access.

## Design
- Scene IR keeps opaque `Rig` stubs (`id` / `format_version` / `payload_ref`).
- The versioned payload is `contracts/schemas/puppet-rig-v1.schema.json` with
  joints, bones, layers, skin bindings, constraints and reference poses.
- Supported retarget profile is only `paper_2d_v1` in 2d space; unknown fields,
  unsupported constraints and budgets fail closed.
- C++ parse/validate lives in `scene-ir` (`load_puppet_rig_json`) so later FK
  work can consume a checked value document.

## Measurement
- none
