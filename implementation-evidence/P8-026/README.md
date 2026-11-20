# Shader and GPU integration CI

## Acceptance

A shader change without the correct layout or golden image blocks the pull
request.

## Decisions

- Add hosted job `gpu-integration` that builds the development preset, verifies
  offline MSL PIN hashes and CPU/GPU layout contracts, then runs CTest `-L gpu`
  (Metal goldens, resource retirement, short video stress).
- Keep certification timings out of this gate: artifact `summary.json` always
  sets `certified_performance: false`.
- Label existing Metal/video tests with `gpu` (and `golden` / `retirement` /
  `video-stress` where applicable) instead of inventing a parallel suite.
- Document the shader change workflow under `engine/shaders/README.md` and the
  required branch-protection check in `tools/toolchain/CI_RELEASE_GATE.md`.

## Verification

- `python3 tools/dev/run_gpu_integration.py -p build/release --artifact-dir implementation-evidence/P8-026/gpu-artifacts`
  result: `passed` (`gpu-artifacts/summary.json`).
- `tools/dev/check.py --keep-going --report implementation-evidence/P8-026/checks.json`
  result: `passed` (release+PTS, ASan+UBSan, authoring/contracts, tools/dev and
  tools/toolchain unit tests). TSan was not selected by path risk.
- Measurement: none. No performance certification claim.

## Evidence tier

A (commands, tests, `checks.json`, anonymized CI artifacts).
