# Historical paper figure reference pack

## Acceptance
- One asset passes rig validation and has goldens for different poses.

## Design
- Value-type pack composes the existing reference human rig topology with
  painted paper layers/skin, the default face expression library, curated
  gestures, and the reference retarget map.
- License metadata is CC0-1.0 with a sidecar LICENSE.txt; no personal identity.
- Named pose evaluation resolves bind, expression ids, or gesture ids into FK
  samples used for locked text goldens.

## Ownership / lifetime
- Pack, samples, and FK skeletons are value types; no shared mutable state.

## Measurement
- none

## Sanitizers
- ASan/UBSan workflow passed as selected by change-risk checks.
