# Production CLI scriptability

The `instantee` binary exposes validate, compile, render, frame, benchmark,
lint, direction-report and help for shell/CI use without a UI. Global
`--json-log` emits NDJSON on stderr; render supports `--progress` frame events
and stable process exit codes with `published`/`durability` on every error.

`render` drives Scene IR → H.264 MP4 through `export_scene_video` and
`AppleVideoSink`. Optional `on_progress` runs synchronously on the caller
thread after each appended frame (no cross-thread handoff). `benchmark` times
load/compile/evaluate for preset `m1-air` and always reports `certified:false`
(measurement: none).

Verification: `instantee_cli_e2e_regression` plus release/ASan/TSan suites.
Evidence tier raised to B for native export linkage and progress callback.
