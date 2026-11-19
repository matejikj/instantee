# Schema versioning and migrations

`schema_version` uses `MAJOR.MINOR.PATCH` without leading zeros.

The machine-readable **supported-version matrix** lives in
[`compatibility/schema-version-matrix-v1.json`](compatibility/schema-version-matrix-v1.json).
Locked load/migrate/reject examples are catalogued in
[`compatibility/locked-examples-v1.json`](compatibility/locked-examples-v1.json).

## Unknown fields

Policy: **reject**.

- JSON Schema documents set `additionalProperties: false` on contract objects.
- C++ loaders reject unknown object keys with stable code `unknown_field` and a
  JSON path before semantic validation.
- There is no silent strip/ignore path for unknown fields in V1.

## Authoring

| Version | Status | Notes |
|---|---|---|
| `1.0.0` | current | Accepted by JSON Schema and the C++ loader |
| `0.9.0` | supported legacy | Pure migration renames `style_package` → `style` and bumps to `1.0.0` |
| other `0.*` / `1.*` | rejected | Explicit `unsupported_schema` |
| `MAJOR >= 2` | rejected | Incompatible major |

Migrations are pure: they allocate a new document, never mutate the caller
buffer, and never touch the network or filesystem. Fixtures live under
`fixtures/authoring/migrations/`. Classification uses
`classify_authoring_schema_version` (`current` / `migratable` / `unsupported`).

## Scene IR

Current accepted version is `1.0.0`. Older IR documents are rejected with
`unsupported_schema` until an explicit migrator and fixtures are added.

## Style Bible

| Version | Status | Notes |
|---|---|---|
| `1.0.0` | current | Typed color/font/material/spacing/motion/camera/safe-area tokens |
| other `1.*` / `0.*` | rejected | Explicit unsupported schema until a migrator exists |
| `MAJOR >= 2` | rejected | Incompatible major |

`package_id` + `version` identify a Style Package revision. Changing Style Bible
rules requires a new `version` (and therefore a new cache key for CDS). The
document must not mutate `core-model` types.

## Asset lockfile, puppet-rig, paper-material-params

Current accepted version is `1.0.0` for each family. Unsupported versions fail
closed with `unsupported_schema` (see the matrix).

## Compatibility suite

- C++: CTest `spike_scene_ir_compatibility_suite` drives the locked-examples
  catalog (load, migrate-and-load, unsupported_schema, unknown_field).
- Contracts: `tools/contracts/check_schema_compatibility.mjs` checks the matrix,
  catalog paths, and migration fixture alignment.
