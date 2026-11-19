# Schema compatibility policy

Normative supported-version matrix and locked-example catalog under
`contracts/compatibility/`. Unknown fields are rejected (`unknown_field`) in
JSON Schema and C++ authoring/Scene IR loaders.

Authoring `0.9.0` remains migratable; other legacy/future versions fail with
`unsupported_schema`. The C++ suite
`spike_scene_ir_compatibility_suite` and
`tools/contracts/check_schema_compatibility.mjs` enforce the catalog.

Measurement: none. Evidence tier A (ASan also run via native change risk).
