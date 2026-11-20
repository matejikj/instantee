# Release-gate CI

## Acceptance

Invalid headers, imports or schemas and sanitizer or concurrency regressions
block the pull request; native coverage limits and development-versus-
certification presets are documented.

## Decisions

- Expand `.github/workflows/ci.yml` from a single development smoke job into
  five required checks: `native-dev`, `native-asan-ubsan`, `native-tsan`,
  `contracts-authoring`, and `dependency-policy`.
- Hosted runners use development sanitizer presets (`dev-asan-ubsan`,
  `dev-tsan`) so CI does not require the reference Xcode/SDK/Node lock.
  Certification sanitizer presets remain for local reference evidence.
- clang-tidy covers every own translation unit present in
  `compile_commands.json` via `tools/dev/run_clang_tidy_own.py`.
- Own targets already compile with `-Werror` through `instantee_build_options`.
- Native coverage is behavioral tests, linters and sanitizers — not line or
  branch percentage gates. Documented in
  `tools/toolchain/CI_RELEASE_GATE.md`.
- Toolchain sandbox tests now copy `check_cpp_api_surface.py` so boundary and
  API CTests succeed in the isolated tree.

## Verification

- `tools/dev/check.py --keep-going --report implementation-evidence/P8-025/checks.json`
  result: `passed` (release build/tests, certification ASan+UBSan, tools/dev
  and tools/toolchain unit tests, tracker sync). TSan was not selected by path
  risk for this change; CI owns the dedicated TSan job.
- `python3 tools/dev/run_clang_tidy_own.py -p build/dev` — 374 own units,
  warnings as errors, passed (`clang-tidy-own-summary.json`).
- Module boundaries, C++ API surface and dependency audit scripts passed.
- Required branch-protection check names listed in `required-checks.json`.
- Measurement: none. No performance certification claim.

## Evidence tier

A (commands, tests, `checks.json`).
