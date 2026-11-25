# Architecture decision record index

Authoritative ADR bodies for V1 live in the local planning pack
`motion_framework_sas/adrs/` (ignored by Git). Versioned contract ADRs that
must ship with the repository live under `contracts/adrs/`.

Use this index to find the decision; open the linked file for the full text.

| ID | Title | Location |
| --- | --- | --- |
| ADR-0001 | Runtime / authoring boundary | `motion_framework_sas/adrs/0001-runtime-authoring-boundary.md` |
| ADR-0002 | Versioned Scene IR (JSON V1) | `motion_framework_sas/adrs/0002-scene-ir-versioning.md` |
| ADR-0003 | Deterministic per-frame evaluation | `motion_framework_sas/adrs/0003-deterministic-animation.md` |
| ADR-0004 | Single Metal backend and style packages | `motion_framework_sas/adrs/0004-single-gpu-backend-style-packages.md` |
| ADR-0005 | Creative direction at compile time | `motion_framework_sas/adrs/0005-compile-time-creative-direction.md` |
| ADR-0006 | Apple VideoSink and frame transfer | `motion_framework_sas/adrs/0006-video-export-adapter.md` |
| ADR-0007 | C++23 modular monolith + TypeScript authoring | `motion_framework_sas/adrs/0007-cpp23-modular-monolith-typescript.md` |
| ADR-0008 | RAII, leases and bounded pipeline | `motion_framework_sas/adrs/0008-raii-bounded-pipeline.md` |
| ADR-0009 | Transactional publish and durability | `motion_framework_sas/adrs/0009-transactional-output.md` |
| ADR-0010 | Reproducible throughput / p99 / memory / CPU | `motion_framework_sas/adrs/0010-reproducible-performance.md` |
| ADR-0011 | Frame transfer: CPU staging vs Metal/IOSurface | `motion_framework_sas/adrs/0011-frame-transfer-path.md` |
| ADR-0012 | P0 feasibility Go/No-Go | `motion_framework_sas/adrs/0012-p0-feasibility-go-no-go.md` |
| ADR-0013 | Scene IR evolution policy | `contracts/adrs/0013-scene-ir-evolution-policy.md` (also mirrored in the planning pack) |
| ADR-0014 | Draft certification thresholds | `motion_framework_sas/adrs/0014-certification-thresholds-draft.md` |
| ADR-0015 | Safe TeX-like math subset V1 | `motion_framework_sas/adrs/0015-math-tex-subset-v1.md` |

When a clone lacks `motion_framework_sas/`, rely on the SAS ADR summary in that
pack when available locally, the English summaries in this index, and the
versioned files under `contracts/adrs/`.
