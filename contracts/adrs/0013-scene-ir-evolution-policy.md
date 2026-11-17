# ADR-0013 — Scene IR evolution policy

- **Status:** Accepted
- **Date:** 2026-11-16 (planning lock); published into `contracts/` with the accepted schemas
- **Context:** Style phases add charts, math layout, diagrams, props, rigs and fold meshes. Without a growth rule, either every style forks the IR or silent schema edits bypass compatibility.
- **Decision:**
  1. Style-level constructs primarily **lower at compile time** onto core primitives (`group` / `geometry` / `text` / `image` / `puppet` and registered materials/features).
  2. The IR gains new node/resource kinds only for **render-semantic** concepts that cannot be expressed as lowering (for example skinned/deformable mesh, fold mesh, versioned rig). Each such extension needs an explicit schema version and fixtures.
  3. Until the compatibility freeze, pre-release schema changes use draft revisions with fixtures; silent edits of a locked schema are forbidden. Data-contract changes keep an explicit `schema_version`.
  4. Rig references stay **opaque and versioned** in the first IR release; the concrete interchange lands with Paper Cinema and must not force early IR churn.
- **Consequences:** CDS/style packages own lowering and configuration. `render-core` consumes registered shader features/materials, not style names. Later style ADRs may extend kinds only with evidence and a version bump.
- **Verification:** Schema negative tests (unknown fields/kinds rejected), compiler lowering goldens (later), and extension-style registry tests that add a synthetic package without changing domain core.
