# Export measurement

Operator playbook (smoke vs certification, common measurement failures):
[`playbooks/benchmark.md`](../../playbooks/benchmark.md). Troubleshooting for
export/GPU/memory faults: [`playbooks/troubleshooting.md`](../../playbooks/troubleshooting.md).

The native `spike-export-metrics` executable composes the existing Metal test
pattern, bounded pipeline, Apple H.264 sink and private output transaction.
It exports 1–9000 frames at 1080×1920, 30/1 fps. The producer uses K=2 credits,
a capacity-one queue and one Metal owner; the serial sink uses an E=8 pixel pool.
Preparation holds all eight pool buffers simultaneously before releasing them,
so the actual allocation peak reaches E without guessing encoder retain counts.
This preparation and its memory cost are part of the measured workload.

```sh
cmake --build --preset release --target spike-export-metrics
mkdir /tmp/instantee-measurement
build/release/spike-export-metrics /tmp/instantee-measurement 30 on
```

The last argument is `on` or `off`. An optional final resource-sample capacity
(default 65536, maximum 65536) supports bounded-overflow tests. Existing output
artifacts are never overwritten by the child. SIGINT/SIGTERM propagate through
the shared stop token; failure stops admission, aborts pending work and joins the
producer before dependencies are destroyed. Submitted GPU work retires normally.
Writer finalization checks stop; publication checks it at its entry gate. The
external harness bounds stuck native calls by terminating the child process.

## Measurement boundary and profiles

`measure.py` uses a monotonic timestamp immediately before `Popen`, then `wait4`
and another timestamp immediately after successful reaping. This includes
startup, render, full video decode/PTS and metadata validation, file sync/publish,
child reports and teardown. Its own report serialization occurs afterwards.
The polling observer sleeps 1 ms between nonblocking wait4 calls; this observation
lag is included in wall time and is never subtracted. It does not call Popen.poll
or wait before wait4, because that would discard the child's resource accounting.
Signals are forwarded; deadlines escalate from SIGTERM to SIGKILL after grace.
`--timeout` and `--grace` are positive finite seconds (defaults 120 and 5);
set them explicitly in a run profile for longer exports.

```sh
python3 tools/benchmark/measure.py build/release/spike-export-metrics \
  /tmp/instantee-measured-run --profile /tmp/measurement-profile.json \
  --frames 30 --telemetry on \
  --schema contracts/schemas/performance-report-v1.schema.json
```

The profile must contain exactly the allowlisted `machine_profile`,
`build_profile` and `workload` fields of the performance-report contract. Supply
verified technical values without personal paths or device identifiers, and pin
the real binary hash, compiler flags, dependency/shader hashes and dirty source
identity before measurement. No ambient machine identity or environment is
collected. Structural checks prefer `contracts/schemas/performance-report-v1.schema.json`
(with a design-pack fallback) and use Python `jsonschema` when installed; the
semantic harness in `performance_report.py` always applies protocol invariants.
Regular workspace tests do not require `jsonschema`.
The child emits its actual sanitizer mode. A sanitizer build is a diagnostic run;
no standalone report certifies performance. RSS budget and p99/CPU thresholds in
this feasibility profile remain proposed/unlocked.

The bundle contains `output/output.mp4`, raw frame/resource samples and child
report, plus `reports/report.json` and `reports/outer.json`. Report references
are relative and checksummed. Each JSON artifact is published independently with
file sync and atomic rename; a later report failure cannot roll back the video.
The child uses exclusive publication; the harness owns its newly created bundle.
Parent metadata-sync failure after report rename is a visible error. Raw child
stdout/stderr are discarded, so native diagnostics cannot leak identity into
shared evidence. Review allowlisted bundle contents before committing any evidence.

## Sample and unit contracts

`frames.json` stores rows in presentation order:

```text
[frame, request_ns, admitted_ns, eval_start_ns, eval_done_ns, gpu_submit_ns,
 gpu_completed_observed_ns, sink_attempt_ns, sink_accepted_ns]
```

All times share the child's steady-clock epoch. Request precedes credit
acquisition. Completion is observed after the readback API returns, including
its CPU copy; this is a host observation, never GPU device time. Sink attempt
precedes flatten/copy/pool waits; accepted follows successful append. Primary
latency is request→accepted. The C++ sink log preallocates one slot per expected
frame, rejects invalid/duplicate/out-of-order records and computes exact
nearest-rank quantiles over all recorded frames. The Python harness independently
checks raw records and the native summary. No frame warm-up is discarded.

`resources.json` rows are:

```text
[observed_ns, user_ns, system_ns, rss_current_bytes, rss_peak_bytes,
 physical_footprint_bytes]
```

The sampler starts before native preparation, samples every 100 ms and records
a final observation after publication. It stops before child report serialization;
wait4 covers CPU and RSS high-water through process exit. Missing API fields are
null. CPU comes from `getrusage(RUSAGE_SELF)` with checked timeval→ns conversion;
RSS/current high-water come from `MACH_TASK_BASIC_INFO`, footprint from
`TASK_VM_INFO.phys_footprint`. Only these technical fields are copied from OS
structures. No UUID or other identity field is recorded.

The parent uses wait4 user+system CPU across all renderer threads. One-core
percentage is `100*(user_ns+system_ns)/wall_ns`, machine percentage divides it by
logical CPU count, and CPU seconds/frame uses verified frames. GPU/encoder service
CPU outside the child process is excluded. Darwin ru_maxrss byte units are
corroborated against Mach's byte-sized high-water counter and a touched 32 MiB
allocation in the native test; historical manuals that say kilobytes are not
used as a conversion rule. RSS and footprint are independent, never added.

Buffers are bounded and sampler shutdown is stop/wake/join. Capacity overflow,
missed sampler periods, missing/reordered timestamps, unavailable required
observations or incomplete frame counts invalidate measurement completeness.
Failed/telemetry-off reports have null effective fps; unavailable device time,
GPU allocation and optional footprint stay null. A zero measurement is retained
only when its API actually returned zero. Every report has `certified=false`.

For telemetry overhead, run matched on/off exports in fresh processes with the
same binary, frame count, profile and conformance/durability mode. Retain every
pair's external wall, CPU and peak RSS, plus the signed difference and ratio.
Off runs intentionally lack primary latency samples and cannot be used as
complete benchmark samples. Timing ratios can be noisy or negative differences;
never subtract estimated overhead from official results. These short trials
are measurement validation, not the later cold/warm baseline or certification.

Primary API references: [Mach task counters](https://github.com/apple/darwin-xnu/blob/main/osfmk/mach/task_info.h),
[Apple process memory guidance](https://developer.apple.com/videos/play/wwdc2022/10106/).

Run three alternating overhead pairs with:

```sh
python3 tools/benchmark/compare.py build/release/spike-export-metrics \
  /tmp/instantee-overhead --profile /tmp/measurement-profile.json --frames 30 \
  --schema contracts/schemas/performance-report-v1.schema.json
```

All six bundles remain available. The comparison refuses sanitized or incomplete
trials and records every signed wall/CPU/RSS difference plus wall ratios.

## B-00 60 s cold/warm baseline

Lock a B-00 profile (`workload.id=B-00-test-pattern-60s-v1`, 1800 frames,
`quality_tier=feasibility-baseline`) before the first series. Then run:

```sh
python3 tools/benchmark/baseline.py build/release/spike-export-metrics \
  /tmp/instantee-b00-baseline --profile /tmp/b00-profile.json \
  --schema contracts/schemas/performance-report-v1.schema.json
```

The series executes at least three cold and three warm measured processes with
telemetry on. Warm mode first runs an uncounted same-workload process, then a
new measured process. The spike has no durable application asset/scene cache, so
warm does not invent one; OS/driver caches remain uncontrolled and are never
labeled as application cold/warm. Each retained report carries explicit
pass/fail for sample completeness and verified frame count. `baseline.json`
summarizes wall/p99/CPU/RSS without certifying throughput limits. Use a long
`--timeout` (default 600 s). Do not commit MP4 binaries; keep checksummed raw
reports under ignored local evidence.

## B-00 300 s thermal and memory endurance

Lock a B-00 300 s profile (`workload.id=B-00-test-pattern-300s-v1`, 9000 frames,
same feasibility-baseline quality tier) before the first endurance series:

```sh
python3 tools/benchmark/endurance.py build/release/spike-export-metrics \
  /tmp/instantee-b00-endurance --profile /tmp/b00-300-profile.json \
  --schema contracts/schemas/performance-report-v1.schema.json
```

Each measured process keeps telemetry on and records RSS/footprint/CPU resource
samples, K/E/queue peaks, the oldest slow frame, clean shutdown and an
allowlisted thermal series sampled during the run. Pass/fail requires complete
verified samples, capacity bounds, no silent drops and bounded post-warmup RSS
growth under the soft/hard budgets used only as anti-leak gates—not as certified
SLO caps. Default `--timeout` is 1800 s.

## Versioned B-01..B-05 workload locks

`contracts/fixtures/benchmark-workloads/catalog-v1.json` consolidates the locked
B-01..B-05 workloads. Offline checksum audit (and intentional baseline bumps):

```sh
python3 tools/benchmark/verify_workload_locks.py --json
```

Regression must compare the same `workload_id` and pinned digests. Changing a
scene, asset lock, style bible, or catalog digest without a new workload id
fails closed. B-04/B-05 locks exist before their first measurement runs.

### B-01/B-02/B-03 60 s style certificate

Run a fresh 3+3 cold/warm series for each locked style reel, then assemble the
shared threshold certificate (individual run reports stay `certified=false`):

```sh
cmake --build --preset release --target \
  spike-b01-editorial-export spike-b02-notebook-export spike-b03-paper-export
python3 tools/benchmark/make_b01_profile.py \
  build/release/spike-b01-editorial-export /tmp/b01-profile.json
python3 tools/benchmark/b01_baseline.py \
  build/release/spike-b01-editorial-export /tmp/b01-series \
  --profile /tmp/b01-profile.json \
  --schema contracts/schemas/performance-report-v1.schema.json
# …repeat for B-02 and B-03 with their binaries/profiles…
python3 tools/benchmark/style_certificate.py \
  --b01 /tmp/b01-series/baseline.json \
  --b02 /tmp/b02-series/baseline.json \
  --b03 /tmp/b03-series/baseline.json \
  --out /tmp/b01-b03-certificate.json
```

Every valid cold trial must meet `export_wall_ms ≤ 360000`. Null p99, RSS, or
CPU ceilings FAIL. Median/min/max are summary only; slow runs are retained.

### B-04/B-05 300 s thermal endurance suite

Stability-only suite (`measurement=none`): fps buckets over time, allowlisted
thermal samples, K/E/queue peaks, error counts, and RAM plateau / growth gates.
Default is one cold and one warm process per locked 300 s family:

```sh
python3 tools/benchmark/make_b04_profile.py \
  build/release/spike-b04-mixed-export /tmp/b04-profile.json
python3 tools/benchmark/make_b05_profile.py \
  build/release/spike-b05-worst-accepted-export /tmp/b05-profile.json
python3 tools/benchmark/style_endurance.py \
  --b04-binary build/release/spike-b04-mixed-export \
  --b04-profile /tmp/b04-profile.json \
  --b05-binary build/release/spike-b05-worst-accepted-export \
  --b05-profile /tmp/b05-profile.json \
  --directory /tmp/b04-b05-endurance \
  --schema contracts/schemas/performance-report-v1.schema.json \
  --timeout 2100
```

Pass/fail rejects fatal post-warmup latency slowdown, capacity overflow, silent
drops, and working-set growth beyond the declared RSS peak/growth budgets. It
does **not** certify throughput, p99, or CPU SLOs. Artifacts: `suite.json` and
`endurance-plot.md`.

### B-04 mixed 300 s reference series

```sh
cmake --build --preset release --target spike-b04-mixed-export
python3 tools/benchmark/make_b04_profile.py \
  build/release/spike-b04-mixed-export /tmp/b04-profile.json
python3 tools/benchmark/b04_baseline.py \
  build/release/spike-b04-mixed-export /tmp/b04-series \
  --profile /tmp/b04-profile.json \
  --schema contracts/schemas/performance-report-v1.schema.json \
  --timeout 2100
```

Requires exactly 9000 frames, quality tier `mixed-b04-v1`, and non-null ADR
thresholds. Every cold trial must meet `export_wall_ms ≤ 1800000`. Run reports
remain `certified=false`.

### B-05 worst-accepted 300 s reference series

```sh
cmake --build --preset release --target spike-b05-worst-accepted-export
python3 tools/benchmark/make_b05_profile.py \
  build/release/spike-b05-worst-accepted-export /tmp/b05-profile.json
python3 tools/benchmark/b05_baseline.py \
  build/release/spike-b05-worst-accepted-export /tmp/b05-series \
  --profile /tmp/b05-profile.json \
  --schema contracts/schemas/performance-report-v1.schema.json \
  --timeout 2100
```

Requires exactly 9000 frames, quality/complexity tier `worst-accepted-b05-v1`,
and non-null ADR thresholds. Publishes the supported complexity ceilings and
performance limits; every cold trial must meet `export_wall_ms ≤ 1800000` with
no OOM. Run reports remain `certified=false`.
