# CI release gate

P1 keeps a minimal green CI smoke path. The release gate expands that path so a
pull request cannot merge while headers, imports, schemas, sanitizers or
concurrency checks are red. Certification performance evidence stays outside CI.

## Required GitHub checks

Configure branch protection on `main` so every pull request must pass these
check names (exact `jobs.<id>.name` values from `.github/workflows/ci.yml`):

| Check name | Blocks on |
|---|---|
| `native-dev` | Failed development build/test, clang-format drift, clang-tidy finding in own code (`-Werror` via `instantee_build_options`) |
| `native-asan-ubsan` | ASan/UBSan regression on the development sanitizer preset |
| `native-tsan` | TSan regression on the development sanitizer preset |
| `contracts-authoring` | Node/TypeScript lock drift, schema/codegen mismatch, authoring typecheck or SDK test failure |
| `dependency-policy` | Module-boundary violation, public API/header inventory failure, unexpected vcpkg/npm dependency |
| `gpu-integration` | Offline MSL PIN drift, CPU/GPU layout contract break, Metal golden/retirement/video-stress failure |
| `visual-regression` | Unreviewed Style Bible golden drift, rejected/missing approval, or failed readability checklist items |
| `quality-lint` | Unapproved lint errors on released examples, failed safe-area/mobile legibility, or unused waivers |
| `security-audit` | Missing license/hash coverage, metal-cpp pin drift, disallowed license, or unresolved critical audit finding |

Do not require certification-preset jobs (`release`, `asan-ubsan`, `tsan` with
`INSTANTEE_TOOLCHAIN_MODE=certification`) on hosted runners: those presets
enforce the reference Xcode/SDK/Node lock and exist for local reference
evidence, not for GitHub Agents.

## Development versus certification presets

| Preset family | Toolchain mode | Purpose |
|---|---|---|
| `dev`, `dev-asan-ubsan`, `dev-tsan` | `development` | Local iteration and CI. Feature probes stay on; exact Xcode/SDK/Node pins are not required. |
| `toolchain`, `release`, `asan-ubsan`, `tsan` | `certification` | Reference-hardware evidence. Exact CMake/Ninja/AppleClang/Xcode/SDK and Node locks apply. |

Sanitizer results from either family are correctness diagnostics only. Never
publish them as certified throughput, p99, CPU or memory numbers. Reference
series and certification claims follow `BENCHMARK_PROTOCOL.md` on the locked
machine profile.

## Native coverage limits

The project does **not** gate merges on line or branch coverage percentages.

Coverage for native code means:

- Behavioral unit, integration and contract tests in CTest
- Isolated public-header compilation negatives
- Module-boundary and C++ API-surface linters
- Full-own-code clang-tidy with warnings as errors
- Separate ASan+UBSan and TSan workflows

Apple system frameworks are not fully instrumented under the project sanitizers.
A green sanitizer job therefore does not imply complete coverage of OS encoder,
Metal driver or filesystem paths. Treat those boundaries with dedicated adapter
tests and export conformance suites instead of coverage counters.

## Local equivalents

```sh
cmake --workflow --preset dev
python3 tools/dev/run_clang_tidy_own.py -p build/dev
cmake --workflow --preset dev-asan-ubsan
cmake --workflow --preset dev-tsan
npm ci --ignore-scripts --no-audit --no-fund
npm run check:toolchain && npm run contracts:check && npm run contracts:test
npm run check:authoring && npm run test:authoring
python3 tools/dev/check_module_boundaries.py
python3 tools/dev/check_cpp_api_surface.py
python3 tools/dev/check_dependency_audit.py
python3 tools/dev/run_gpu_integration.py -p build/dev --artifact-dir /tmp/gpu-artifacts
python3 tools/dev/run_visual_regression.py -p build/dev --artifact-dir /tmp/visual-artifacts --enforce-platform
python3 tools/dev/check_quality_release.py --write-report /tmp/quality-lint-report.json
python3 tools/dev/check_security_audit.py --write-report /tmp/security-audit-report.json --write-sbom /tmp/sbom.json
```

A `.metal` source change must update `engine/shaders/modules/PIN.json` (via
`tools/dev/check_shader_pin.py --write` after intentional edits) and keep
`contracts/shaders/cpu-gpu-layouts-v1.json` aligned with `sizeof` static_asserts.
Golden PNG/hash expectations in Metal tests must be refreshed deliberately; an
unreviewed shader output change fails `gpu-integration`. Artifact
`summary.json` always sets `certified_performance: false` — this gate is not a
release timing profile.

Style Bible PNG/structural goldens are locked by
`contracts/visual-baseline/registry-v1.json` to the Apple Silicon macOS Metal
family. Updating a style golden requires a `CHANGE_RECORD.json` entry and an
approved `visual-review/review-record.json`; otherwise `visual-regression`
fails the pull request. Readability checklist items from the registry must stay
passed.

Released examples are gated by `contracts/quality-release/policy-v1.json`.
Unwaived error findings, failed phone/safe-area legibility, or unused waivers
fail `quality-lint`. The uploaded report sets `certified_performance: false`.

Distributed fonts/textures, vendored pins and lock manifests are gated by
`tools/dev/check_security_audit.py`. Drift in metal-cpp identity, missing
license sidecars, release-asset-lock digest mismatch or a disallowed license
fails `security-audit`. The job publishes an offline SBOM; it does not fetch
CVE feeds and sets `certified_performance: false`.

On the reference machine, prefer the certification sanitizer presets when
collecting evidence that must match the toolchain lock.
