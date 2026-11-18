#pragma once

#include <instantee/core_model/depth_stage.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/paper_prop_blueprint.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::render_core {

// Seeded Paper Cinema background kit: mountains, clouds, architecture, trees,
// and atmospheric cards placed on a depth-stage stack. Generation is a pure
// function of seed + limits (SAS §7.1). Asset counts are hard-capped.

enum class BackgroundElementKind : std::uint8_t {
  mountain = 1,
  cloud = 2,
  building = 3,
  tree = 4,
  atmosphere = 5,
};

struct BackgroundKitLimits {
  std::uint32_t max_total_assets = 48;
  std::uint32_t max_mountains = 8;
  std::uint32_t max_clouds = 12;
  std::uint32_t max_buildings = 10;
  std::uint32_t max_trees = 16;
  std::uint32_t max_atmosphere = 6;
  // Depth-stage plane count (inclusive of kit layers). Must stay in [10, 20].
  std::uint32_t stage_layers = 12;
};

struct BackgroundKitParams {
  std::uint64_t seed = 1;
  double stage_width = 1080;
  double stage_height = 1920;
  BackgroundKitLimits limits{};
};

// Flat decorative card used for mountain/tree/atmosphere (no new prop kinds).
struct BackgroundSilhouettePart {
  std::string id;
  PaperPropPartShape shape = PaperPropPartShape::rect;
  Rect2f local_bounds{};
  std::string palette_slot;
};

struct BackgroundElement {
  BackgroundElementKind kind = BackgroundElementKind::mountain;
  std::string id;
  std::uint32_t stage_layer = 0;
  double depth = 1;
  double offset_x = 0;
  double offset_y = 0;
  float scale = 1;
  // Populated for cloud/building via paper prop blueprints.
  ResolvedPaperProp prop{};
  bool has_prop = false;
  // Populated for mountain/tree/atmosphere silhouettes.
  std::vector<BackgroundSilhouettePart> silhouette{};
};

struct BackgroundScenery {
  std::uint64_t seed = 0;
  core::DepthStage stage{};
  std::vector<BackgroundElement> elements{};
  std::uint32_t asset_count = 0;
};

[[nodiscard]] constexpr std::string_view
background_element_kind_id(BackgroundElementKind kind) noexcept {
  switch (kind) {
  case BackgroundElementKind::mountain:
    return "mountain";
  case BackgroundElementKind::cloud:
    return "cloud";
  case BackgroundElementKind::building:
    return "building";
  case BackgroundElementKind::tree:
    return "tree";
  case BackgroundElementKind::atmosphere:
    return "atmosphere";
  }
  return "unknown";
}

[[nodiscard]] BackgroundKitLimits background_kit_limits_v1() noexcept;

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_background_kit_params(const BackgroundKitParams &params);

// Generate stable scenery for the seed. Identical inputs ⇒ identical outputs.
// Fails closed when limits are invalid or prop instantiation fails.
[[nodiscard]] std::expected<BackgroundScenery, diagnostics::Error>
generate_paper_background_scenery(const BackgroundKitParams &params);

[[nodiscard]] std::string
format_paper_background_scenery(std::string_view label,
                                const BackgroundScenery &scenery);

} // namespace instantee::render_core
