# Repository architecture

Instantee is a TypeScript authoring and C++23 runtime project. The current
implementation contains a native feasibility workspace and a private TypeScript
authoring package shell. Video export, the authoring API and performance
measurement are not implemented yet.

## Current layout

```text
engine/                    Native runtime component (C++23 / Objective-C++)
  apps/cli/                Entry points and private argument parser
  src/diagnostics/         Platform-neutral diagnostic value types
  src/core-model/          Platform-neutral frame clock and rational time types
  src/render-metal/        Metal device/queue/test-pattern/readback backend
  shaders/spike/           Spike MSL sources embedded into the Metal backend
  native/export-apple/     Apple adapter (pixel-buffer pool + H.264 video writer)
  tests/                   Native unit, contract and subprocess tests
third_party/metal-cpp/     Pinned metal-cpp headers with archive checksum metadata
authoring/                 Authoring component (TypeScript)
  packages/authoring/      Private SDK package shell
  tsconfig.base.json       Shared strict compiler configuration
contracts/                 Shared data boundary and future schemas/fixtures
benchmarks/                Checked entry point for the future benchmark process
cmake/                     Toolchain lock, probes and shared native build options
tools/                     Development scripts and their tests/documentation
```

The root `CMakeLists.txt` selects the language standard, verifies the toolchain
and composes `engine/` and `benchmarks/`. Each native library, application and
test group owns its targets in a local `CMakeLists.txt`. Shared warning and
sanitizer settings are applied through `instantee_build_options`.

The root `package.json` owns npm workspace commands, the pinned compiler and
one shared lockfile. Each authoring package owns its manifest and compiler
configuration. Native and authoring build products stay under ignored `build/`.

## Engine–authoring boundary

TypeScript authoring will produce a versioned document. The native engine will
validate that document and execute it through a file/CLI boundary. Node.js and
the TypeScript compiler do not participate in the native per-frame loop.
The current empty SDK package does not implement that exchange yet.

Accepted schemas and cross-language fixtures belong in `contracts/`, with
serialization implementations in their owning components. Neither component
imports the other's implementation. See [the boundary guide](contracts/README.md).

`authoring/` describes the SDK's responsibility; it is not a graphical frontend.
A future editor should be a separate application that consumes the SDK and
engine boundary without introducing UI dependencies into either component.

## Dependency and visibility rules

```text
spike CLI ──> CLI parser ──> diagnostics
    ├──────> Apple adapter ──> diagnostics
    │                 └──────> core-model
    └──────> render-metal ──> diagnostics

core-model ──> diagnostics
```

The diagnostics module has no application or native dependency. The core-model
module stays platform-neutral and must not include Metal, AVFoundation or CLI
headers. The Apple adapter may use core-model time types for presentation
timestamps, but keeps AVFoundation inside Objective-C++. The Apple adapter and
Metal backend do not include CLI headers. The executables compose the parser and
adapters; application policy stays at that composition boundary. The current
diagnostic types remain in the `instantee::spike` namespace and are provisional,
not a stable production API.

Reusable libraries expose headers under
`include/instantee/<module>/`. Their `.cpp`, `.mm` and internal headers live in
`src/`. CMake publishes only the `include/` directory. Consumers include public
headers by their qualified names, for example
`<instantee/export_apple/workspace.hpp>`.

The application parser is private to `engine/apps/cli`; it is not a domain library.
Tests may explicitly access a module's private headers when testing internal
ownership behavior. That access is private to the test target and is not
propagated to other consumers.

Apple frameworks and Objective-C++/ARC are confined to the native adapter, the
Metal backend and their native tests. Public headers use C++ types only. The
public-header contract target compiles as C++ without linking adapters or Apple
frameworks. Build integration tests inspect transitive includes and verify that
private CF, Metal and CLI headers are unavailable through the public include
paths. Pinned `metal-cpp` headers live under `third_party/metal-cpp/` and are
private to `render-metal`.

## Growth rules

Create a directory when it gains its first implementation or executable
contract. Do not create placeholder modules for the entire roadmap.

- Add platform-neutral domain and runtime modules under `engine/src/`, such as
  `core-model`, `scene-ir`, `compiler`, `animation`, `render-core`, `export-core`
  and `runtime`. Each has a cohesive responsibility and explicit dependencies.
- Keep the Metal C++ backend in `engine/src/render-metal/` and Objective-C++ export
  integration in `engine/native/export-apple/`. Domain modules never include backend
  implementation details.
- Add TypeScript SDK and style helper packages under `authoring/packages/` when
  implemented. Shared schema sources belong in `contracts/`.
- Keep style resources under `styles/`, native shader sources under
  `engine/shaders/`, and reproducible workload fixtures/profiles under
  `benchmarks/` as needed. TypeScript style helpers stay in authoring packages.
- Group native unit tests by their owning module under `engine/tests/` and
  authoring-specific tests inside their packages. Add contract, integration,
  golden and fault tests when the corresponding behavior exists; shared
  cross-language fixture data belongs in `contracts/fixtures/`.

Prefer a specific module name over generic `common`, `utils` or `core` buckets.
Changes in one backend should be isolated by its existing public contracts.
Introduce a new abstraction only for a concrete dependency or testing need.

## Versioned and local material

Source code, build definitions, tests, this architecture guide and the
contribution guide are versioned and usable without the local planning pack.
Tool-specific instructions stay beside their tools and are linked from the
guides.

`motion_framework_sas/` is an ignored local design and planning pack.
`implementation-evidence/` is ignored local verification evidence. Both remain
available locally and must not be force-added or published. The implementation
tracker may refer to local evidence; a clone does not contain that evidence.
Build products, dependencies and caches are also ignored.

See [CONTRIBUTING.md](CONTRIBUTING.md) for build, verification and change rules.
