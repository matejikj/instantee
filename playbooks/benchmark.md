# Benchmark playbook

How to measure export performance without confusing diagnostic runs for
certified M1 Air results. Normative measurement rules live in the planning pack
`motion_framework_sas/BENCHMARK_PROTOCOL.md` when present. Operator detail for
harnesses is in [`tools/benchmark/README.md`](../tools/benchmark/README.md).

## When to use which path

| Goal | Tooling | Certification? |
| --- | --- | --- |
| CLI smoke / tutorials | `instantee render --frames N` | **No** |
| Feasibility export metrics | `spike-export-metrics` + `tools/benchmark/measure.py` | Only with locked profile + protocol series |
| Style 60 s / 300 s series | `tools/benchmark/*_baseline.py`, `style_endurance.py` | Only on reference hardware with complete evidence |
| Sanitizer / ASan / TSan | `dev-asan-ubsan`, `dev-tsan`, certification sanitizer presets | **Never** (correctness only) |

## Preconditions (every serious run)

1. Release (or certification) build **without** sanitizers.
2. Allowlisted `machine_profile` / `build_profile` / `workload` JSON — technical
   fields only; no host names, user paths or device UUIDs.
3. Pin binary hash, compiler flags and workload locks before measuring.
4. Quiet thermal host for prescribed cold and warm reference series; record
   thermal samples when the harness requires them.
5. Set `performance_claim` / certified flags only after protocol gates pass.

## Smoke measurement (not certified)

```sh
cmake --build --preset release --target spike-export-metrics
mkdir -p /tmp/instantee-measurement
build/release/spike-export-metrics /tmp/instantee-measurement 30 on
```

Use this to verify the pipeline wires. Do **not** file the bundle as M1 Air
certification evidence.

## Harness measurement

```sh
python3 tools/benchmark/measure.py build/release/spike-export-metrics \
  /tmp/instantee-measured-run \
  --profile /path/to/allowlisted-profile.json \
  --frames 30 --telemetry on \
  --schema contracts/schemas/performance-report-v1.schema.json
```

Inspect `reports/report.json` and `reports/outer.json`. Semantic failures
(count/formula/quantile/missing metrics) invalidate certification even if an
MP4 exists.

## Style endurance (uncertified unless gated)

```sh
python3 -m unittest discover -s tools/benchmark -p 'test_style_endurance.py' -v
```

B-04/B-05 helpers document RAM plateau and growth gates. Treat suite outputs as
uncertified unless a separate reference-series gate records a pass on the locked
machine.

## Common measurement failures

| Failure | Diagnostics | Action |
| --- | --- | --- |
| Schema/semantic reject | Harness error; fixture under `contracts/fixtures/performance-report/semantic-negative/` | Fix counts/formulas; do not edit limits to force PASS |
| Sanitizer binary | Child report sanitizer mode ≠ none | Rebuild release/certification; discard numbers |
| Incomplete thermal/metrics | Null mandatory fields | Re-run with required samplers; incomplete ≠ PASS |
| Tutorial smoke frames | Short `--frames` MP4 only | Keep under tutorials; never promote to reference series |

## Evidence hygiene

- Keep raw stdout/stderr and absolute paths out of committed evidence
- Prefer relative URIs inside report bundles
- Review allowlists before force-adding anything under `implementation-evidence/`
