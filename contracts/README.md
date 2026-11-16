# Engine–authoring data boundary

This directory is reserved for the versioned data contracts shared by the native
engine and TypeScript authoring packages: accepted schemas, compatibility rules
and cross-language positive/negative fixtures as they are implemented.

There is no finalized production schema here yet. Draft schemas in the ignored
local design pack are planning inputs, not an implemented public protocol.

Shared contracts must describe data, without C++ object layouts, native handles,
live TypeScript functions or renderer dependencies. Authoring produces documents;
the engine validates and consumes them through the agreed file/CLI boundary.
Schema changes require explicit versions and compatibility tests.

Keep language-specific serialization implementations in `engine/` and
`authoring/`. Place shared schemas under `schemas/` and cross-language fixtures
under `fixtures/` when they become real implementation inputs. Do not duplicate
independently maintained schemas inside both language components.
