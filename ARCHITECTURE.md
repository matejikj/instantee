# Repository architecture

Instantee is a TypeScript authoring SDK plus a C++23 native engine that compiles
Style Bible–directed films to Scene IR and exports deterministic H.264 video on
Apple Silicon. Implementation details for day-to-day work live under
[architecture/](architecture/README.md).

## Start here

| Question | Document |
| --- | --- |
| Who owns a native module? | [Module catalog](architecture/modules.md) |
| How does data move authoring → MP4? | [Data flow](architecture/data-flow.md) |
| What are ownership and job-phase rules? | [Ownership and job phases](architecture/ownership-and-jobs.md) |
| How do I extend a style without GPU handles? | [Extending a style](architecture/extending-styles.md) |
| Where are ADRs? | [ADR index](architecture/adr-index.md) |

Normative dependency edges: `cmake/module_graph.json` (enforced by
`tools/dev/check_module_boundaries.py`). Public C++ surface:
`tools/dev/check_cpp_api_surface.py`.

## Layout

```text
engine/                    C++23 / Objective-C++ runtime
  apps/                    CLI and composition entry points
  src/<module>/            Production libraries (see module catalog)
  native/*-apple/          Apple adapters (telemetry; export/transfer spikes)
  shaders/                 MSL sources for the Metal backend
  tests/                   Unit, contract, integration, golden tests
authoring/                 TypeScript workspace (`@instantee/authoring`)
contracts/                 Schemas, fixtures, versioned ADRs, release locks
assets/                    Per-style fonts/textures and offline locks
cmake/                     Toolchain lock, module graph, build options
tools/                     Dev gates, benchmarks, workspace helpers
third_party/               Vendored pins (metal-cpp, fonts, decoders, …)
architecture/              Implementation architecture guides
```

## Engine–authoring boundary

TypeScript produces versioned authoring JSON. The native engine validates and
executes through file/CLI boundaries. Node.js does not participate in the
per-frame loop. Shared schemas and fixtures live in `contracts/`; neither side
imports the other's implementation. Style packages are data consumed by
`direction` and the compiler — not Metal backend variants.

## Dependency rules (summary)

```text
authoring SDK ──JSON──► compiler ──► direction + scene-ir ──► runtime RenderJob
                                              │
                         render-core ◄─────────┴──► preview / export
                              │
                         render-metal (private metal-cpp)
                              │
                         export-core ports ◄── export-apple (spike)
```

- Domain modules (`core-model`, `direction`, `scene-ir`, `geometry`, `math`, …)
  never include Metal, AVFoundation or CLI headers.
- `diagnostics` has no application dependency.
- Spike modules (`export-apple`, `transfer-apple`) are not linked from
  production libraries.

## Growth rules

Create a directory when it gains its first implementation. Prefer a specific
module name over `common`/`utils`. Introduce abstractions only for a concrete
dependency or test seam. Keep public headers under
`include/instantee/<module>/` and implementations in `src/`.

## Versioned vs local material

Source, build definitions, tests and these guides are versioned.
`motion_framework_sas/` (design pack) and `implementation-evidence/` stay local
and ignored. Build products live under ignored `build/`.

See [CONTRIBUTING.md](CONTRIBUTING.md) for verification and change rules.
