#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/scene_ir/document.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace instantee::scene_ir {

// Declared scene complexity ceilings for a named certification/draft tier.
// Exceeding any non-zero limit returns resource_budget_exceeded before the
// runtime attempts large allocations. Zero means "unset / do not enforce".
struct SceneComplexityLimits {
  std::uint32_t max_nodes = 0;
  std::uint32_t max_node_depth = 0;
  std::uint32_t max_assets = 0;
  std::uint32_t max_geometry = 0;
  std::uint32_t max_text_runs = 0;
  std::uint64_t max_text_codepoints = 0; // proxy for glyph pressure
  std::uint32_t max_draw_items = 0;      // geometry+text+image+puppet nodes
  std::uint32_t max_mask_materials = 0;  // shader_feature contains mask/clip
  std::uint32_t max_pixel_layers =
      0; // max non-group nodes referenced by a shot
  std::uint32_t max_animation_tracks = 0;
  std::uint64_t max_keyframes = 0;
  std::uint32_t max_shots = 0;
  std::uint32_t max_canvas_width_px = 0;
  std::uint32_t max_canvas_height_px = 0;
};

struct SceneComplexityTier {
  std::string id;
  SceneComplexityLimits limits{};
};

// Measured counts for one SceneDocument. Units are structural counts, not RSS.
struct SceneComplexityMetrics {
  std::uint32_t nodes = 0;
  std::uint32_t node_depth = 0;
  std::uint32_t assets = 0;
  std::uint32_t geometry = 0;
  std::uint32_t text_runs = 0;
  std::uint64_t text_codepoints = 0;
  std::uint32_t draw_items = 0;
  std::uint32_t mask_materials = 0;
  std::uint32_t pixel_layers = 0;
  std::uint32_t animation_tracks = 0;
  std::uint64_t keyframes = 0;
  std::uint32_t shots = 0;
  std::uint32_t canvas_width_px = 0;
  std::uint32_t canvas_height_px = 0;
};

// Draft locked at P3 gate; numeric caps tightened for pre-OOM rejection.
[[nodiscard]] SceneComplexityTier
complexity_tier_p3_foundation_draft() noexcept;

[[nodiscard]] std::expected<SceneComplexityTier, diagnostics::Error>
lookup_complexity_tier(std::string_view tier_id);

[[nodiscard]] SceneComplexityMetrics
measure_scene_complexity(const SceneDocument &document) noexcept;

// Fail-closed: first exceeded limit wins with a stable error code.
[[nodiscard]] std::expected<void, diagnostics::Error>
check_scene_complexity(const SceneDocument &document,
                       const SceneComplexityTier &tier);

} // namespace instantee::scene_ir
