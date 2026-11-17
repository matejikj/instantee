# Native engine

This is the C++23/Objective-C++ side of Instantee. It owns validation and runtime
execution as those features are implemented, independently of the TypeScript
authoring process. The current implementation contains the native spike CLI,
diagnostics, a rational frame clock, frame-credit admission with bounded
stop-aware queues and reorder slots, transactional output publication with
durable-local sync, a headless Metal device probe, an offscreen test-pattern
renderer with completion-owned readback leases, a bounded Core Video
pixel-buffer pool, an AVFoundation video-only writer adapter with exact-frame
sample durations, SDR Rec.709 color tags, straight-alpha flatten over black,
presentation-timeline and color roundtrip validation, a Metal-to-MP4
composition path exercised by native tests, an isolated Metal/IOSurface
transfer comparison, and Apple resource smoke checks.

```text
apps/cli/              Application entry points and private argument parser
apps/transfer-compare/ Isolated staging vs shared-surface timing harness
src/diagnostics/       Production Error/ExitCode value types
src/core-model/        Platform-neutral frame clock and rational time types
src/scene-ir/          Scene IR load, security limits and graph validation
src/runtime/           Frame credits, bounded queues and reorder slots
src/export-core/       Output transactions, VideoSink/FrameBridge ports, publish
src/geometry/          Path flatten, fill tessellation port, stroke expand, mesh cache
src/render-core/       GPU-neutral DrawOp/PassDesc, primitives, graph, registry, pools, features, cache
src/render-metal/      Production Metal device/submit owner, SDF primitives, offline metallib modules
shaders/               Pinned MSL sources and module manifest; metallibs compiled offline
native/                Objective-C++ adapters; export/transfer remain spike
tests/                 Native unit, isolated public-header and subprocess tests
```



Each module owns its `CMakeLists.txt`. Public library headers live under
`include/instantee/<module>/`; implementation files and private headers live under
`src/`. Application internals are not exposed as runtime library APIs.

Configure and build from the repository root, where the toolchain, presets and
shared verification entry points are defined:

```sh
cmake --workflow --preset release
build/release/spike-render --smoke
```

See [the development guide](../CONTRIBUTING.md),
[architecture rules](../ARCHITECTURE.md) and
[CLI/workspace guide](../tools/workspace/README.md).
