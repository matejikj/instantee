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
                (authoring, scene-ir, asset-lockfile)
  fixtures/     Cross-language positive/negative examples
                (plus authoring/migrations and scene-ir/semantic-negative)
  defaults/     Documented omitted-field defaults
  generated/ts/ Checked-in TypeScript types from schemas
  adrs/         Published data-contract ADRs
  VERSIONING.md Schema semver + pure migration policy
  invariants-sas6.json  Coverage map for SAS §6.2 invariants
  INTEGER_ENCODING.md
  README.md
```


Keep language-specific serializers in `engine/` and `authoring/`. Do not
duplicate independently maintained schemas inside both language components.

## Tooling

```sh
npm run contracts:generate   # regenerate TypeScript types
npm run contracts:check      # drift + fixture schema validation
npm run contracts:test       # encoding + cross-language suite
```
