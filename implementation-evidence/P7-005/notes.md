# Layered 2.5D depth stage

## Acceptance
- A stage of 10–20 paper planes responds to camera truck/pan with monotonic
  parallax: nearer layers move more on screen than farther ones.
- Screen motion under a pure lateral truck scales with `parallax_factor`
  (`focus_depth / layer_depth`); axes stay uninverted.

## Design
- Value types live in `core-model` (`depth_stage.hpp`) beside the camera math.
- `StageAnchor` and `LayerClip` are layout concepts separate from
  `LocalTransform` pivot (SAS §8.1).
- Planes sit at world `z = -depth`; projection reuses `make_perspective_camera`
  and `project_world_to_pixel`. No render/runtime dependency.
- Compiler/style lowering of the `depth_stage` package component remains a
  later delivery; this slice defines the spatial contract used by those paths.

## Measurement
- none
