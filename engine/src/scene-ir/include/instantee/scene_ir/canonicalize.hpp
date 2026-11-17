#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace instantee::scene_ir {

// Deterministic JSON canonicalization for cache/provenance keys.
// Object keys are sorted lexicographically; arrays keep order; integers are
// decimal without exponent; floats use a fixed round-trip format. Never uses
// std::hash or C++ object padding.
[[nodiscard]] std::expected<std::string, diagnostics::Error>
canonicalize_json(std::string_view json);

[[nodiscard]] std::expected<std::string, diagnostics::Error>
sha256_hex(std::span<const std::byte> bytes);

[[nodiscard]] std::expected<std::string, diagnostics::Error>
canonical_sha256_hex(std::string_view json);

} // namespace instantee::scene_ir
