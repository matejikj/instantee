# Troubleshooting runbook

Each case below documents a common failure with reproduction steps and the
diagnostics to collect. Prefer repository-relative paths. Do not paste host
names, user home directories, hardware UUIDs or private URLs into shared
evidence.

CLI errors print one JSON object on stderr with at least `code`, `phase`,
`message`, `published` and `durability`. Exit codes match
`instantee::diagnostics::ExitCode` in
`engine/src/diagnostics/include/instantee/diagnostics/error.hpp`.

## Case: export_failure

### Symptoms

`instantee render` stops before a durable MP4, or the process exits with
`export_failure` / `publication_conflict` / `published_durability_unknown`.

### Reproduction

```sh
# Missing Scene IR (invalid path)
build/release/instantee render /nonexistent/scene.json --out /tmp/out.mp4
echo $?   # expect 2 (invalid_input) with code=io_error

# Valid compile then render to a non-writable destination (export/publish path)
build/release/instantee compile contracts/fixtures/authoring/positive/minimal.authoring.json \
  --out /tmp/scene.json \
  --style-bible contracts/fixtures/style-bible/positive/editorial.style.json
build/release/instantee render /tmp/scene.json --out /nonexistent-dir/out.mp4
```

### Diagnostics

- stderr JSON: `code`, `phase` (`render`, `export`, `publish`, …), `published`,
  `durability` (`not_published`, `confirmed`, …)
- Exit code: `6` (`export_failure`), `9` (`publication_conflict`),
  `10` (`published_durability_unknown`), or `2` for earlier input/IO faults
- Optional `--json-log` NDJSON on stderr for progress around the failing phase
- Confirm whether a staging file exists beside the destination (transactional
  publish leaves no half-renamed final on exclusive publish failure)

### Next steps

- Fix destination directory permissions or choose `--overwrite` when replacing
  an existing file intentionally
- If `published=true` with durability unknown, treat the MP4 as present but
  verify checksum/size before relying on it
- Do not re-run measurement on a failed publish and claim success

## Case: missing_or_corrupt_font

### Symptoms

Compile/render fails while resolving typography, or glyphs are missing /
fallback fonts unexpectedly appear. Exit kind often
`missing_or_corrupt_asset` (exit `3`) or `invalid_input` when the Style Bible /
lock points at a bad path.

### Reproduction

```sh
# Point Style Bible / asset lock tooling at a removed font pin (dry check)
python3 - <<'PY'
from pathlib import Path
p = Path("third_party/fonts/NotoSans-Regular.ttf")
print("font_present", p.is_file())
print("license_present", Path("third_party/fonts/OFL-Noto-Sans.txt").is_file())
PY

# Offline security/license gate surfaces hash or license sidecar gaps
python3 tools/dev/check_security_audit.py
```

### Diagnostics

- `third_party/fonts/PIN.json` `archive_sha256` vs file bytes
- Release asset lock entries with `role=font` and `license_path`
- CLI stderr `code` / `json_path` when authoring references an unresolved asset
- Confirm no implicit system font catalog is being relied on (typography module
  uses pinned paths only)

### Next steps

- Restore pinned TTF + license sidecar; re-verify with
  `tools/dev/check_security_audit.py`
- Do not copy fonts from personal Library paths into evidence

## Case: color_mismatch

### Symptoms

Preview/export looks washed out, too dark, or channels disagree between PNG
preview and MP4. Often a working-color / transfer misunderstanding rather than a
GPU fault.

### Reproduction

```sh
# Authoring canvas color space is locked in the document
python3 - <<'PY'
import json
from pathlib import Path
doc = json.loads(Path("contracts/fixtures/authoring/positive/minimal.authoring.json").read_text())
print(doc["canvas"]["color"])  # expect rec709-sdr
PY

# Single-frame evaluate (optional PNG) for visual comparison
build/release/instantee compile contracts/fixtures/authoring/positive/minimal.authoring.json \
  --out /tmp/scene.json \
  --style-bible contracts/fixtures/style-bible/positive/editorial.style.json
build/release/instantee frame /tmp/scene.json --at 0 --out /tmp/frame0.png
```

### Diagnostics

- Authoring `canvas.color` (V1 expects `rec709-sdr`)
- Style Bible color tokens use `space: linear-rgba` in fixtures
- Preview path is Rec.709/sRGB encode in `preview` / `export` modules
- Visual baseline tolerances under `contracts/fixtures/*/visual-review/`

### Next steps

- Compare PNG preview against style goldens; do not “fix” by changing Metal
  color spaces ad hoc
- Keep linear-light authoring tokens; encode only at the preview/export boundary

## Case: gpu_device_loss

### Symptoms

Render fails with `gpu_failure` (exit `5`), Metal device creation fails, or a
device-loss style error appears mid-export. Hosted CI without Apple Silicon Metal
cannot reproduce production GPU paths.

### Reproduction

```sh
# Capability / device presence (development machine)
cmake --build --preset release --target instantee
# A missing Metal device typically surfaces as gpu_failure during render:
build/release/instantee render /tmp/scene.json --out /tmp/out.mp4 --frames 1

# Offline shader/PIN gate (does not need a live encode)
python3 tools/dev/check_shader_pin.py
python3 tools/dev/check_shader_layouts.py
```

### Diagnostics

- Exit code `5` / kind `gpu_failure`; stderr `code` and `phase`
- `engine/shaders/modules/PIN.json` and metallib build logs
- Confirm process is not running under a sanitizer-only path when diagnosing
  driver issues (sanitizer builds are correctness tools, not GPU cert runs)
- Note OS family/arch only (`macos` / `arm64`); never record hardware UUID

### Next steps

- Restore offline metallibs / PIN alignment; re-run `gpu-integration` locally on
  Apple Silicon
- On device loss mid-job, treat output as unpublished unless `published=true`
- Do not claim certified performance from a recovered GPU retry

## Case: memory_pressure

### Symptoms

Job stops with `resource_budget_exceeded` (exit `16`) or `out_of_memory`
(exit `17`). RSS grows beyond the declared budget on endurance runs.

### Reproduction

```sh
# Contractual budget surfaces on RenderJob (unit/integration coverage)
ctest --preset release -R resource_budget --output-on-failure

# Endurance suite documents RAM plateau / growth gates (uncertified)
python3 -m unittest discover -s tools/benchmark -p 'test_style_endurance.py' -v
```

### Diagnostics

- Exit code `16` or `17`; stderr `code` / `phase` / optional `frame`
- Performance report `resources` peaks and null-vs-present metrics
- K/E/queue capacity settings (defaults K=2, queue 1, E=8 on spike paths)
- Whether the binary was built with ASan (higher RSS; never certify)

### Next steps

- Reduce in-flight frames / scene complexity; keep queues bounded
- Re-run endurance only after a clean release build without sanitizers
- Missing mandatory memory metrics invalidate certification claims
  (`tools/benchmark/README.md`, performance-report contract)

## Case: measurement_invalid

### Symptoms

A performance report is rejected by schema/semantic checks, or a smoke/tutorial
run is mistaken for M1 Air certification.

### Reproduction

```sh
# Semantic negatives exist under contracts/fixtures/performance-report/
python3 - <<'PY'
from pathlib import Path
root = Path("contracts/fixtures/performance-report/semantic-negative")
print(sorted(p.name for p in root.glob("*.json"))[:5])
PY

# Harness structure checks (no certification claim)
python3 -m unittest discover -s tools/benchmark -p 'test_*.py' -v
```

### Diagnostics

- `performance_claim` / `certified` fields must stay false unless a locked
  reference series on the profiled machine passed
- Sanitizer mode in the child report
- Formula/count/quantile mismatches listed in harness errors
- Machine profile allowlist only (no personal identity fields)

### Next steps

- Follow [benchmark.md](benchmark.md) for cold/warm reference series
- Discard sanitizer or tutorial `--frames` smoke outputs from certification
  evidence packs
