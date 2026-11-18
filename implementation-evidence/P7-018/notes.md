# Paper curvature deformation

## Acceptance
- Parametric bend/wave keep a watertight silhouette (no cracked shared edges,
  no collapsed edges) and stay inside `paper-curvature-lite-v1` op budgets.

## Design
- Regular grid mesh + CPU reference deform (bend arc / sinusoidal wave).
- Complexity tier caps grid resolution and total vertex ALU ops; oversize
  grids and out-of-range amplitudes fail closed.
- Shader feature `paper_curvature` registers one vertex-deform pass key;
  inactive materials add no passes (empty-pass invariant).

## Ownership / lifetime
- Meshes/poses are value types; no shared mutable deformer state.
- Benchmark binary is process-local; no GPU buffers retained across frames.

## Measurement
- Targeted Google Benchmark via `paper-curvature-bench`, packed by
  `tools/benchmark/paper_curvature_bench.py` → `paper_curvature_report.json`.
- Certification: `non_certified_dev_baseline` (not an M1 Air product series).
- Raw Google Benchmark JSON kept outside the repository.
