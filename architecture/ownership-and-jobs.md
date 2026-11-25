# Ownership and job phases

Normative detail lives in `motion_framework_sas/CPP_RUNTIME_CONTRACTS.md` when
the planning pack is present. This page is the implementation map.

## Ownership invariants

- Prefer Rule of Zero value types in domain modules.
- Exclusive resources use move-only owners (`unique_ptr`, module-specific
  leases). Destructors only release; fallible finalize/publish is explicit.
- Borrowed spans/views must not outlive their owner, realloc or arena reset.
- Queues and pools are bounded. Failed push never silently grows capacity.
- Stop tokens propagate through admission, queues, pools and the serial export
  owner. Abort extracts pending ownership before destroying queue storage.
- Apple frameworks and `metal-cpp` stay behind private adapter/backends. Public
  headers expose C++ types only (`tools/dev/check_cpp_api_surface.py`).

## `RenderJob` phase machine

Owner: `engine/src/runtime/` (`instantee::runtime::RenderJob`). Workers and GPU
resources stay outside this type; the job tracks phase, cancel gate, sticky
error and resource budget.

```text
created
  → preparing
  → rendering
  → finishing
  → publishing
  → published ──► completed
         │
         ├─► published_durability_unknown   (terminal)
         └─► published_report_failed        (terminal)

Any pre-publish phase may become failed or cancelled.
Cancel may win only before publishing; after the publish gate the outcome
stays a published* phase.
```

Export orchestration in `engine/src/export/` advances these phases while
composing preview raster, `VideoSink` accept and `OutputTransaction` publish.

## Related modules

| Concern | Owner |
| --- | --- |
| Frame credits (K), bounded queues, reorder | `runtime` |
| Staging publish / durability result | `export-core` |
| Scene export orchestration | `export` |
| Mach/process sampling | `telemetry-apple` |
| Signal → stop bridge | workspace/CLI composition (`tools/workspace/README.md`) |
