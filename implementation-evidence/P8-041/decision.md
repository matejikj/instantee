# Decision: metric reproducibility audit

## Decision

Accept the reproducibility audit without re-running reference exports:

1. Exact nearest-rank and CPU normalization oracles match known counters; the
   overflow-safe quantile formula agrees with the Python harness on large N.
2. Lost samples, truncated frames, design fixtures, and missing mandatory
   metrics cannot become certification-eligible PASS.
3. Pinned B-01/B-02/B-03 3× cold/warm series summaries independently recompute
   from committed run rows (medians match). Run reports remain `certified=false`.
4. Telemetry overhead evidence stays a non-certified experiment; it is not a
   performance PASS and is never subtracted from official results.

## Non-claims

This audit does not certify M1 Air wall/p99/CPU/memory thresholds. Fresh
measurement requires `measurement=reference-series` or `benchmark` tasks.
