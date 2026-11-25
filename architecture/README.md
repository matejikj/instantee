# Implementation architecture

Practical map of the Instantee codebase for contributors. Normative design
contracts remain in the local planning pack
(`motion_framework_sas/SOFTWARE_ARCHITECTURE_SPECIFICATION.md` when present)
and in `CPP_RUNTIME_CONTRACTS.md` / `BENCHMARK_PROTOCOL.md`.

| Document | Use when you need to |
| --- | --- |
| [Module catalog](modules.md) | Find which directory owns a concern and what it may depend on |
| [Data flow](data-flow.md) | Trace authoring → direction → Scene IR → render/export |
| [Ownership and job phases](ownership-and-jobs.md) | RAII leases, bounded queues, `RenderJob` state machine |
| [Extending a style](extending-styles.md) | Add or change a Style Bible package without touching GPU backends |
| [ADR index](adr-index.md) | Locate architecture decision records |

The dependency graph in `cmake/module_graph.json` is enforced by
`tools/dev/check_module_boundaries.py`. Architecture doc coverage is checked by
`tools/dev/check_architecture_docs.py`. These guides are versioned under
`architecture/` (the ignored `/docs/` tree is for local notes only).
