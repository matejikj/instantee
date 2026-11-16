# Native engine

This is the C++23/Objective-C++ side of Instantee. It owns validation and runtime
execution as those features are implemented, independently of the TypeScript
authoring process. The current implementation contains the native spike CLI,
diagnostics, a rational frame clock, a headless Metal device probe, an offscreen
test-pattern renderer with completion-owned readback leases, a bounded Core Video
pixel-buffer pool, an AVFoundation video-only writer adapter with exact-frame
sample durations, presentation-timeline extraction/validation, a Metal-to-MP4
composition path exercised by native tests, and Apple resource smoke checks.

```text
apps/cli/          Application entry points and private argument parser
src/diagnostics/   Platform-neutral diagnostic value types
src/core-model/    Platform-neutral frame clock and rational time types
src/render-metal/  Metal device, queue, test-pattern and readback leases
shaders/spike/     Spike MSL sources embedded into render-metal
native/            Objective-C++ adapters with private implementation details
tests/             Native unit, public-header contract and subprocess tests
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
