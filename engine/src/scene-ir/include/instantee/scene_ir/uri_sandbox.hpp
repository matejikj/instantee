#pragma once

#include <instantee/diagnostics/error.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace instantee::scene_ir {

// Relative asset URIs must stay under project_root/assets after
// weakly_canonical (symlink-aware). Absolute paths, `..`, backslashes, URI
// schemes (`:`), empty segments (`//`), and symlink escapes outside `assets/`
// fail. Ownership of the policy stays in this scene-ir security helper —
// loaders opt in via LoadOptions::enforce_uri_sandbox.
[[nodiscard]] std::expected<std::filesystem::path, diagnostics::Error>
resolve_asset_uri(const std::filesystem::path &project_root,
                  std::string_view uri);

// Lexical-only gate used before any filesystem touch. Must reject network
// schemes and traversal without consulting the disk.
[[nodiscard]] bool is_safe_asset_uri_syntax(std::string_view uri) noexcept;

} // namespace instantee::scene_ir
