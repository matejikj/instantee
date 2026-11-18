# Paper Cinema B-03 optimization and threshold gate

## Acceptance
Every valid certification cold run of the 60 s historical reel has export wall
≤360 s, meets the pre-locked p99/memory/CPU profile, and carries complete CPU
evidence.

## What changed
- ADR-0014 gains `paper-b03-v1` ceilings derived from the measured B-03 series
  with documented headroom on p99/CPU; the measurement profile pins them before
  the gate series. Null thresholds FAIL.
- Demo smoke asserts shot-local title/figure opacity fade-out across the reveal
  → object-closeup boundary so inactive proxies leave the transparent-skip path.
- Profiling note: Metal shadow passes and skinned-mesh GPU work are not on this
  critical path (CPU Scene IR → preview proxy raster → Apple VideoSink). Top
  overdraw cost is mitigated by existing shot-local fade-outs plus shared
  transparent-skip; persistent stage/background layers remain intentional
  paper-atmosphere cost.

## Measurement
- measurement=`reference-series` (3 cold + 3 warm).
- Observed gate series medians: cold wall ≈105.6 s, p99 ≈65.0 ms,
  RSS ≈114.4 MiB, CPU ≈0.0598 s/frame — all within locked ADR ceilings.
- Individual run reports remain `certified=false`; series `threshold_gate=true`.
- Not an M1 Air product certificate; allowlisted machine/OS facts only.

## Ownership / lifetime / concurrency
See `ownership-review.json`.

## Verification
- `b03_baseline.py` series_pass + cold_wall_within_360s
- `instantee_b03_paper_demo` (opacity lifecycle + 60-frame smoke)
- `tools/dev/check.py --asan --pts`
