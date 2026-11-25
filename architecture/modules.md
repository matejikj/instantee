# Module catalog

Each native library owns one directory, one CMake target and one public include
prefix. The table is the contributor-facing view of
`cmake/module_graph.json`. Edges are normative: a production module may depend
only on listed production modules.

## How to find the owner

1. Identify the concern (time types, Scene IR, style direction, Metal draw, …).
2. Match it to a row below (`Path` is the owning tree).
3. Read public headers under `include/<Include prefix>`.
4. Confirm new edges against `allowed_dependencies` before editing CMake.

Domain and direction code must never include Metal, AVFoundation or
`third_party/metal-cpp` headers. Those stay private to adapter modules.

## Catalog

| Module | Path | Include prefix | Role | May depend on |
| --- | --- | --- | --- | --- |
| `diagnostics` | `engine/src/diagnostics/` | `instantee/diagnostics/` | production | — |
| `core-model` | `engine/src/core-model/` | `instantee/core_model/` | production | diagnostics |
| `runtime` | `engine/src/runtime/` | `instantee/runtime/` | production | diagnostics, core-model |
| `export-core` | `engine/src/export-core/` | `instantee/export_core/` | production | diagnostics, core-model |
| `telemetry-apple` | `engine/native/telemetry-apple/` | `instantee/telemetry_apple/` | production | diagnostics |
| `render-metal` | `engine/src/render-metal/` | `instantee/render_metal/` | production | diagnostics, render-core, typography |
| `export-apple` | `engine/native/export-apple/` | `instantee/export_apple/` | spike | diagnostics, core-model, export-core |
| `transfer-apple` | `engine/native/transfer-apple/` | `instantee/transfer_apple/` | spike | diagnostics, render-metal, export-apple |
| `direction` | `engine/src/direction/` | `instantee/direction/` | production | diagnostics, core-model |
| `compiler` | `engine/src/compiler/` | `instantee/compiler/` | production | diagnostics, core-model, scene-ir, runtime, direction |
| `scene-ir` | `engine/src/scene-ir/` | `instantee/scene_ir/` | production | diagnostics, core-model |
| `geometry` | `engine/src/geometry/` | `instantee/geometry/` | production | diagnostics |
| `math` | `engine/src/math/` | `instantee/math/` | production | diagnostics |
| `asset-pipeline` | `engine/src/asset-pipeline/` | `instantee/asset_pipeline/` | production | diagnostics, geometry |
| `typography` | `engine/src/typography/` | `instantee/typography/` | production | diagnostics, asset-pipeline, core-model |
| `render-core` | `engine/src/render-core/` | `instantee/render_core/` | production | diagnostics, core-model |
| `preview` | `engine/src/preview/` | `instantee/preview/` | production | diagnostics, core-model, runtime, render-core |
| `export` | `engine/src/export/` | `instantee/export/` | production | diagnostics, core-model, export-core, runtime, preview |

## Authoring and contracts

| Area | Path | Notes |
| --- | --- | --- |
| TypeScript SDK | `authoring/packages/authoring/` | Public surface: `@instantee/authoring` ([PUBLIC_API.md](../authoring/packages/authoring/PUBLIC_API.md)) |
| Shared schemas/fixtures | `contracts/` | Cross-language boundary; no engine internals |
| Style Bible fixtures | `contracts/fixtures/style-bible/` | Token packages for the three V1 styles |
| Style asset locks | `assets/<style>/` | Fonts, textures, locks; offline verify only |

## Spike isolation

`export-apple` and `transfer-apple` remain spikes. Production libraries must not
link them or include their headers. Composition belongs in apps/CLI and tests.
