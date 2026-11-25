# Changelog

All notable changes to the Instantee framework are documented in this file.

Version axes are independent (see `contracts/release/version-manifest-v1.json`):

- **framework_semver** — product/CLI/SDK identity (`vMAJOR.MINOR.PATCH` tags)
- **scene_ir_schema_version** — on-disk Scene IR `schema_version`
- **style_package_version** — per Style Bible `package_id` + `version`

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Framework versions use [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- (none yet)

## [1.0.0] — 2026-11-25

First public V1 release identity. Compatible contract versions and migrations
are declared in `contracts/release/version-manifest-v1.json` and
`contracts/compatibility/schema-version-matrix-v1.json`.

### Breaking Changes

- Public product identity is **framework_semver 1.0.0**. Pre-1.0 experiment
  tags and informal `0.x` packaging labels are not supported as upgrade paths.
- Scene IR accepts only **schema_version 1.0.0**. Older Scene IR documents
  (for example `0.9.0`) are rejected with `unsupported_schema` until an
  explicit migrator and fixtures are added.
- Style Bible packages ship as **editorial@1.0.0**,
  **scientific-notebook@1.0.0**, and **paper-cinema@1.0.0**. Consumers must pin
  `package_id` + package semver; mixing unpinned style bytes is unsupported.
- Authoring documents on **0.8.0** and newer-than-current majors are rejected.
  Authoring **0.9.0** remains loadable only via the documented migrator
  `migrate_authoring_json` (fixture
  `contracts/fixtures/authoring/migrations/from-0.9.0`).
- Unknown object fields in Authoring / Scene IR / Style Bible JSON are
  rejected (`unknown_field`); silent ignore is not part of the V1 contract.
- macOS distribution for V1 is the relocatable CLI package with embedded
  metallibs and **ad-hoc** signing. Developer ID + notarization is a separate
  distribution channel, not implied by this release.

### Added

- Separated release version axes and a normative compatibility matrix for the
  `v1.0.0` tag.
- Relocatable macOS arm64 CLI packaging with install smoke (no host Metal
  shader compile).
- Schema compatibility policy with locked examples and authoring 0.9.0 → 1.0.0
  migration.
- Three V1 Style Bible packages, quality-release policy, security/SBOM gate,
  architecture docs, tutorials, and troubleshooting/benchmark playbooks.

### Changed

- CMake `project(... VERSION ...)` and `@instantee/authoring` package version
  align to **framework_semver 1.0.0** for the declared release identity.

### Compatibility (v1.0.0)

| Axis | Supported |
| --- | --- |
| framework_semver | 1.0.0 |
| scene_ir_schema_version | 1.0.0 |
| authoring_schema_version | 1.0.0 (current), 0.9.0 (migratable) |
| style packages | editorial@1.0.0, scientific-notebook@1.0.0, paper-cinema@1.0.0 |
