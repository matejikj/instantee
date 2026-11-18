# Irregular-cut paper silhouettes

## Acceptance
- Seeded edge irregularities produce the same outline and fill mesh across
  repeated compiles (frame-stable; no time-based noise).
- `amplitude_px` cannot exceed `safe_inset_px`; cut vertices are clamped so the
  axis-aligned safe text rectangle stays covered by the fill.
- Outer outline is the single silhouette used for fill tessellation and as the
  mask boundary identity (`paper_cut_content_key` + `PathGeometryCache`).

## Design
- Lives in the `geometry` module next to path tessellation / ink stroke so
  render-core stays free of path types.
- Compile-once fill meshes go through `PathGeometryCache`; warm lookups record
  a single tessellation compile.
- Edge geometry golden: `contracts/fixtures/paper-cut/goldens/rect-seed42.txt`.

## Measurement
- none
