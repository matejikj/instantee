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
- Designated integers beyond JS safe range (seed/hash) use the encoding policy
  locked in P1; there is no silent precision loss.
- Schema changes require explicit versions, fixtures and compatibility tests.
- Completing the domain/IR phase requires this directory to contain the accepted
  authoring and Scene IR schemas plus the contract fixtures needed for clean
  clone CI. Drafts that live only in the ignored local design pack are not
  sufficient for production gates.

## Layout

```text
contracts/
  schemas/     Accepted JSON Schema documents
  fixtures/    Cross-language positive/negative examples
  README.md    This boundary guide
```

Keep language-specific serializers in `engine/` and `authoring/`. Do not
duplicate independently maintained schemas inside both language components.
