#pragma once

#include <instantee/scene_ir/document.hpp>
#include <instantee/scene_ir/limits.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string_view>

namespace instantee::scene_ir {

struct LoadOptions {
  InputLimits limits{};
  // When set, asset URIs are resolved and must remain under this root.
  std::filesystem::path project_root{};
  bool enforce_uri_sandbox = false;
};

// Parse + structural validate Scene IR JSON. Does not touch the network.
// Semantic graph checks are a separate step (validate_graph).
[[nodiscard]] std::expected<SceneDocument, diagnostics::Error>
load_scene_json(std::span<const std::byte> bytes, const LoadOptions &options);

[[nodiscard]] std::expected<SceneDocument, diagnostics::Error>
load_scene_json(std::string_view text, const LoadOptions &options);

[[nodiscard]] std::expected<SceneDocument, diagnostics::Error>
load_scene_file(const std::filesystem::path &path, const LoadOptions &options);

} // namespace instantee::scene_ir
