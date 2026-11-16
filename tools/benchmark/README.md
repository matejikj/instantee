# Export measurement

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
  --schema motion_framework_sas/schemas/performance-report-v1.schema.json
```

The profile must contain exactly the allowlisted `machine_profile`,
`build_profile` and `workload` fields of the performance-report contract. Supply
verified technical values without personal paths or device identifiers, and pin
the real binary hash, compiler flags, dependency/shader hashes and dirty source
identity before measurement. No ambient machine identity or environment is
collected. A schema check requires Python `jsonschema`; the optional local design
pack supplies the schema. Regular workspace tests do not require that pack.
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
  --schema motion_framework_sas/schemas/performance-report-v1.schema.json
```

All six bundles remain available. The comparison refuses sanitized or incomplete
trials and records every signed wall/CPU/RSS difference plus wall ratios.

## B-00 60 s cold/warm baseline

Lock a B-00 profile (`workload.id=B-00-test-pattern-60s-v1`, 1800 frames,
`quality_tier=feasibility-baseline`) before the first series. Then run:

```sh
python3 tools/benchmark/baseline.py build/release/spike-export-metrics \
  /tmp/instantee-b00-baseline --profile /tmp/b00-profile.json \
  --schema motion_framework_sas/schemas/performance-report-v1.schema.json
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
