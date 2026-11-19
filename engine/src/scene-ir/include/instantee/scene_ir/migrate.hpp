#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace instantee::scene_ir {

inline constexpr std::string_view kAuthoringSchemaCurrent = "1.0.0";
inline constexpr std::string_view kAuthoringSchemaLegacy090 = "0.9.0";

struct SemVer {
  std::uint32_t major = 0;
  std::uint32_t minor = 0;
  std::uint32_t patch = 0;
};

// How a parsed authoring schema_version is handled by the compatibility matrix.
enum class SchemaVersionSupport : std::uint8_t {
  current = 0,
  migratable = 1,
  unsupported = 2,
};

// Parse MAJOR.MINOR.PATCH with no leading zeros (except a lone 0 component).
[[nodiscard]] std::expected<SemVer, diagnostics::Error>
parse_schema_semver(std::string_view text);

// Classify an authoring schema_version against the supported-version matrix.
// Invalid semver yields an error; known-but-unsupported versions return
// SchemaVersionSupport::unsupported (callers map that to unsupported_schema).
[[nodiscard]] std::expected<SchemaVersionSupport, diagnostics::Error>
classify_authoring_schema_version(std::string_view version);

// Pure authoring migration: returns a new JSON document at the current schema.
// The input buffer is never modified. Unsupported majors/versions fail with
// ExitCode::incompatible_schema. Already-current documents are returned as an
// owned copy of the original bytes (no network, no filesystem side effects).
[[nodiscard]] std::expected<std::string, diagnostics::Error>
migrate_authoring_json(std::span<const std::byte> bytes);

} // namespace instantee::scene_ir
