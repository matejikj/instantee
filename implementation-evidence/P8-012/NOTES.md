# Extended Scene IR / authoring fuzz corpora

Expands the P1 bounded fuzz skeleton into a deterministic corpus driver.

## Coverage

- `load_scene_json` (parse + structural limits)
- `validate_graph` (cycles, dangling refs, keyframe/shot bounds)
- `load_authoring_json` (authoring tree)
- `FrameIndex::checked_add` and `FrameClock` PTS/duration on accepted docs

## Bounds

Inputs are clipped to `InputLimits::max_bytes`. The harness also tightens
`max_depth`/`max_values` during fuzz runs. Mutations are a fixed 12-way
schedule (cap 256), no OS entropy.

Local seeds live under `engine/apps/fuzz-scene-ir/corpus/`; CI also walks
`contracts/fixtures/{scene-ir,authoring}`.

Measurement: none. Evidence tier A (ASan on native driver).
