# B-03 historical Paper Cinema reel (60 s)

## Acceptance
The story is created entirely by the framework as a silent 1080×1920/30 fps
MP4 assembled only through Scene IR → preview raster → Apple VideoSink (no
external editor).

## What changed
- Paper-cinema offline assets (font + synthetic textures) with PIN/lockfile.
- Locked Scene IR fixture and workload lock under `contracts/fixtures/b03-paper/`.
- Five shot templates packed to 1800 frames: reveal stage, object closeup,
  puppet dialogue, technical explanation, atmospheric dissolve.
- Historical puppet pack provenance via Scene IR rig payload_ref; fold hinge
  and depth layers authored as geometry/image nodes with animation tracks.
- Instrumented export binary verifies the asset lock offline and publishes with
  encoder pool capacity E=8.
- Cold/warm reference-series harness for the locked B-03 profile.
- Sixty-frame integration smoke for CI without a full 60 s render.
- Scene generator `tools/benchmark/gen_b03_scene.py` regenerates the fixture.

## Measurement
- measurement=`reference-series` (3 cold + 3 warm process trials, 1800 frames).
- Profile pins scene/asset/style/quality/encoder hashes plus allowlisted
  machine/build facts.
- Series is measured and uncertified: product wall ≤360 s certification and
  locked p99/CPU/RSS thresholds remain a later optimization gate.
- MP4 binaries stay in local temp only; evidence retains checksums and reports.

## Ownership / lifetime
- Export child owns the Scene IR compile, RenderJob slots, preview raster
  buffers, AppleVideoSink (E=8), and OutputTransaction staging→publish.
- Parent harness owns spawn→wait4 accounting; stop uses the shared stop token
  plus SIGTERM→SIGKILL escalation in the existing measure helper.
- No durable application asset/scene cache across processes; warm only runs an
  uncounted same-workload process before the measured one.

## Verification
- Full 1800-frame demo render + media probe (1080×1920, silent, H.264).
- `b03_baseline.py` series with six complete trials.
- `instantee_b03_paper_demo` smoke (60 frames).
- `tools/dev/check.py` with native/PTS risk selection → `checks.json`.

## Privacy
- Evidence stores allowlisted machine/OS facts only (model class, OS build,
  logical CPU, RAM bytes). No hostnames, usernames, or absolute personal paths.
