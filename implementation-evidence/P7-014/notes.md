# Puppet gestural clips

## Acceptance
- Curated raise-hand, point, grasp, look-at, and nod clips evaluate
  reproducibly on the reference-human rig via a retargetable role contract.

## Design
- Clips author against semantic roles (`shoulder_r`, `elbow_r`, `hand_r`,
  `head`, `spine`, …). A retarget map binds roles to concrete joint ids.
- Sampling lerps keyframe poses in normalized time `[0, 1]` and fails closed
  on missing retarget entries. Evaluation reuses the existing FK evaluator.
- Ownership is value-based: clips, samples, and posed skeletons are plain
  structs; no shared mutable state or async lifetimes.

## Measurement
- none

## Sanitizers
- ASan/UBSan workflow passed as selected by change-risk checks.
