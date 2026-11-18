# Paper prop blueprint model

## Acceptance
- Instrument, building and cloud blueprints compose multiple flat parts with
  named palette/material slots.
- The same blueprint instantiates with alternate palettes or materials while
  preserving part topology (ids, shapes, bounds, slots, depth/layer order).

## Design
- Lives in `render-core` so material params can bind without pulling geometry
  into style packages. Parts stay axis-aligned flat shapes; vector path parts
  can lower later via the compiler.
- Missing slot bindings fail closed. Snapshot goldens lock blueprint layout and
  default resolved bindings.

## Measurement
- none
