# `@instantee/authoring` public API v1.0

Import only from `@instantee/authoring`. Deep imports into `src/`, `generated/`,
or build output paths are outside the freeze contract.

## Runtime namespaces

| Export | Role |
| --- | --- |
| `film` | `create`, `toJson`, `createIntent`, `intentTypes` |
| `direction` | typed semantic intents (`reveal`, `explain`, …) |
| `sceneGraph` | `collectNodeIds`, `parentIdOf`, `reparent` |
| `time` | shared seconds→frames conversion |
| `styles` / `STYLE_PACKAGES` | locked V1 style tags |
| `api` | version, deprecations, typed-error helpers, namespaces |

Flat helpers (`createFilm`, `createIntent`, `filmToJson`, scene-graph and time
functions) remain supported aliases of the same implementations.

## Typed errors

SDK validation throws `AuthoringError` with a stable `code`. TypeScript is a
convenience layer; C++ schema validation remains the security boundary.

## Deprecation policy

See `policy.ts`. Removing or renaming a public export is a MAJOR change.
Changing a default that alters rendered output needs review and either a MAJOR
bump or an explicit opt-in replacement. Deprecated symbols stay callable for at
least one MINOR after announcement and emit `deprecate()` diagnostics.

## Style packages

- `editorial@1`
- `scientific-notebook@1`
- `paper-cinema@1`

## Contract types

Generated authoring / Scene IR / Style Bible types are re-exported for typing
only. They are not a substitute for C++ validation.
