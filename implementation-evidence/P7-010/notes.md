# Skeletal FK and transform propagation

## Acceptance
- Rotating the shoulder moves the elbow and hand; world matrices stay equal
  to `parent_world * local` with no inconsistent rig state.

## Design
- Lives next to the puppet-rig document in `scene-ir` and reuses
  `core::local_matrix` / `combine_parent_local` (SAS §8.1).
- Joint pivots are bind-space positions; local bind translation is
  `child_pivot - parent_pivot`. Animated rotation is about the joint origin.
- Skeleton joints are ordered parents-before-children. Optional hinge limits
  clamp `rotation_z` during evaluation; unknown pose ids and non-finite values
  fail closed.

## Measurement
- none
