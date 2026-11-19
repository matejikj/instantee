# Engine–authoring data boundary

This directory holds the versioned data contracts shared by the native engine
and TypeScript authoring packages: accepted schemas, compatibility rules and
cross-language positive/negative fixtures.

## Rules

- Shared contracts describe data only. No C++ object layouts, native handles,
  live TypeScript functions or renderer dependencies.
- Authoring produces documents; the engine validates and consumes them through
  the file/CLI boundary.
- JSON Schema under `schemas/` is the source of truth. TypeScript types are
  generated from it. C++ defaults are tested against schema defaults.
- Designated integers beyond the JS safe range (`seed` and future `uint64`
  fields) follow [INTEGER_ENCODING.md](INTEGER_ENCODING.md). There is no silent
  precision loss.
- Schema changes require explicit versions, fixtures and compatibility tests.
- Scene IR growth follows [adrs/0013-scene-ir-evolution-policy.md](adrs/0013-scene-ir-evolution-policy.md).

## Layout

```text
contracts/
  schemas/      Accepted JSON Schema documents (Draft 2020-12)
                (authoring, scene-ir, style-bible, asset-lockfile,
                 paper-material-params, puppet-rig)
  fixtures/     Cross-language positive/negative examples
                (plus authoring/migrations, scene-ir/semantic-negative,
                 style-bible, paper-material, puppet-rig)
  defaults/     Documented omitted-field defaults
  generated/ts/ Checked-in TypeScript types from schemas
  adrs/         Published data-contract ADRs
  VERSIONING.md Schema semver + pure migration policy
  invariants-sas6.json  Coverage map for SAS §6.2 invariants
  cpp/          Frozen C++ public-header inventory and API-surface fixtures
  compatibility/ Supported-version matrix and locked load/migrate examples
  INTEGER_ENCODING.md
  VERSIONING.md
  README.md
```

C++ public headers are listed in `cpp/public-headers-v1.snapshot.json` and
enforced by `tools/dev/check_cpp_api_surface.py` (CTest `cpp_api_surface`).

Schema compatibility policy: `compatibility/schema-version-matrix-v1.json`,
`VERSIONING.md` (unknown fields = reject), and CTest
`spike_scene_ir_compatibility_suite`.


Keep language-specific serializers in `engine/` and `authoring/`. Do not
duplicate independently maintained schemas inside both language components.

## Tooling

```sh
npm run contracts:generate   # regenerate TypeScript types
npm run contracts:check      # drift + fixture schema validation
npm run contracts:test       # encoding + cross-language suite
```
