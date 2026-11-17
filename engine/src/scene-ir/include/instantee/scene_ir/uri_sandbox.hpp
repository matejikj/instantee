#pragma once

#include <instantee/diagnostics/error.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace instantee::scene_ir {

// Relative asset URIs must stay under project_root/assets after
// canonicalization. Absolute paths, `..`, backslashes, network schemes and
// symlink escapes fail.
[[nodiscard]] std::expected<std::filesystem::path, diagnostics::Error>
resolve_asset_uri(const std::filesystem::path &project_root,
                  std::string_view uri);

[[nodiscard]] bool is_safe_asset_uri_syntax(std::string_view uri) noexcept;

} // namespace instantee::scene_ir
