# Native engine

This is the C++23/Objective-C++ side of Instantee. It owns validation and runtime
execution as those features are implemented, independently of the TypeScript
authoring process. The current implementation contains the native spike CLI,
diagnostics, a headless Metal device probe and Apple resource smoke checks.

```text
apps/cli/          Application entry points and private argument parser
src/diagnostics/   Platform-neutral diagnostic value types
src/render-metal/  Metal device and queue ownership behind a C++ API
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
