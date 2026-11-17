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

// Parse MAJOR.MINOR.PATCH with no leading zeros (except a lone 0 component).
[[nodiscard]] std::expected<SemVer, diagnostics::Error>
parse_schema_semver(std::string_view text);

// Pure authoring migration: returns a new JSON document at the current schema.
// The input buffer is never modified. Unsupported majors/versions fail with
// ExitCode::incompatible_schema. Already-current documents are returned as an
// owned copy of the original bytes (no network, no filesystem side effects).
[[nodiscard]] std::expected<std::string, diagnostics::Error>
migrate_authoring_json(std::span<const std::byte> bytes);

} // namespace instantee::scene_ir
