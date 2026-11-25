# Data flow

High-level path from authoring input to published MP4. Arrows are ownership or
validated document hand-off, not thread topology.

```text
TypeScript SDK (@instantee/authoring)
    │  AuthoringFilm JSON (schemas in contracts/)
    ▼
compiler  ──uses──►  direction (Style Bible + intents → DirectedFilm)
    │
    │  Scene IR JSON / in-memory IR (scene-ir)
    ▼
runtime CompiledScene / RenderJob
    │
    ├─► preview     (CPU Rec.709/sRGB frames, PNG)
    └─► export      (orchestrates phases)
            │
            ├─► render-core DrawOp/PassDesc (GPU-neutral)
            │       └─► render-metal (private Metal / metal-cpp)
            ├─► export-core VideoSink / FrameBridge / OutputTransaction
            │       └─► export-apple AVFoundation adapter (spike, private)
            └─► transactional publish (staging → final path)
```

## Boundaries that must stay intact

- **Authoring ↔ engine:** file/CLI/JSON only. Node does not join the per-frame
  loop.
- **direction ↔ GPU:** `direction` depends only on `diagnostics` and
  `core-model`. Style tokens and composition constraints never carry Metal
  handles.
- **render-core ↔ Metal:** draw lists and pass descriptors are CPU values;
  `render-metal` is the sole production Metal owner.
- **export-core ↔ AVFoundation:** encoder types stay inside `export-apple`.

## Style-related path

Style Bible JSON → `direction` package load/lint → compiler lowering into Scene
IR nodes/materials → `render-core` feature flags / materials → backend. Extending
a style edits Style Bible tokens, asset locks and authoring style tags; it does
not open `third_party/metal-cpp` or `MTL*` types. See
[extending-styles.md](extending-styles.md).
