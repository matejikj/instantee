# Schema versioning and migrations

`schema_version` uses `MAJOR.MINOR.PATCH` without leading zeros.

## Authoring

| Version | Status | Notes |
|---|---|---|
| `1.0.0` | current | Accepted by JSON Schema and the C++ loader |
| `0.9.0` | supported legacy | Pure migration renames `style_package` → `style` and bumps to `1.0.0` |
| other `0.*` / `1.*` | rejected | Explicit `unsupported_schema` |
| `MAJOR >= 2` | rejected | Incompatible major |

Migrations are pure: they allocate a new document, never mutate the caller
buffer, and never touch the network or filesystem. Fixtures live under
`fixtures/authoring/migrations/`.

## Scene IR

Current accepted version is `1.0.0`. Older IR documents are rejected until an
explicit migrator and fixtures are added.

## Style Bible

| Version | Status | Notes |
|---|---|---|
| `1.0.0` | current | Typed color/font/material/spacing/motion/camera/safe-area tokens |
| other `1.*` / `0.*` | rejected | Explicit unsupported schema until a migrator exists |
| `MAJOR >= 2` | rejected | Incompatible major |

`package_id` + `version` identify a Style Package revision. Changing Style Bible
rules requires a new `version` (and therefore a new cache key for CDS). The
document must not mutate `core-model` types.
