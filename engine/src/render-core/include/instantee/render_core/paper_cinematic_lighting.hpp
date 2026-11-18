#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/paper_layered_shadow.hpp>
#include <instantee/render_core/render_graph.hpp>
#include <instantee/render_core/shader_features.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::render_core {

// Lighting recipes for Paper Cinema shot templates. Keys are capability /
// feature ids — never Style Bible package names. Templates in direction select
// a recipe; render-core builds the shared graph without style-name branches.
enum class PaperCinematicLightingRecipe : std::uint8_t {
  reveal_key_fill = 1,
  closeup_rim = 2,
  dialogue_soft = 3,
  technical_flat = 4,
  atmospheric_veil = 5,
};

inline constexpr std::string_view kPaperLightingRevealKeyFill =
    "paper_lighting_reveal_key_fill";
inline constexpr std::string_view kPaperLightingCloseupRim =
    "paper_lighting_closeup_rim";
inline constexpr std::string_view kPaperLightingDialogueSoft =
    "paper_lighting_dialogue_soft";
inline constexpr std::string_view kPaperLightingTechnicalFlat =
    "paper_lighting_technical_flat";
inline constexpr std::string_view kPaperLightingAtmosphericVeil =
    "paper_lighting_atmospheric_veil";

struct PaperCinematicLightingParams {
  PaperCinematicLightingRecipe recipe =
      PaperCinematicLightingRecipe::reveal_key_fill;
  Rgba8 clear_color{232, 222, 204, 255};
  Rgba8 key_color{255, 248, 236, 255};
  Rgba8 fill_color{210, 200, 188, 255};
  Rgba8 veil_color{40, 48, 64, 0};
  PaperLayeredShadowParams shadow{};
  // Soft overlay for atmospheric dissolve (0 = off, 1 = opaque veil).
  float veil_opacity = 0.0f;
  std::vector<std::string> active_feature_ids{};
};

struct PaperCinematicShotGraphRequest {
  PaperCinematicLightingRecipe recipe =
      PaperCinematicLightingRecipe::reveal_key_fill;
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  // Subject and support cards in pixel space (paper cutouts).
  Rect2f subject{};
  Rect2f support{};
  float subject_depth = 4.0f;
  float support_depth = 8.0f;
  // Optional dissolve veil; recipes other than atmospheric_veil ignore >0.
  float dissolve_progress = 0.0f;
  std::uint64_t output_resource = 1;
  std::uint64_t shadow_resource = 2;
};

[[nodiscard]] constexpr std::string_view paper_cinematic_lighting_recipe_id(
    PaperCinematicLightingRecipe recipe) noexcept {
  switch (recipe) {
  case PaperCinematicLightingRecipe::reveal_key_fill:
    return kPaperLightingRevealKeyFill;
  case PaperCinematicLightingRecipe::closeup_rim:
    return kPaperLightingCloseupRim;
  case PaperCinematicLightingRecipe::dialogue_soft:
    return kPaperLightingDialogueSoft;
  case PaperCinematicLightingRecipe::technical_flat:
    return kPaperLightingTechnicalFlat;
  case PaperCinematicLightingRecipe::atmospheric_veil:
    return kPaperLightingAtmosphericVeil;
  }
  return "unknown";
}

[[nodiscard]] std::expected<PaperCinematicLightingRecipe, diagnostics::Error>
parse_paper_cinematic_lighting_recipe(std::string_view text);

[[nodiscard]] PaperCinematicLightingParams
make_paper_cinematic_lighting(PaperCinematicLightingRecipe recipe) noexcept;

// Registers lighting recipe feature ids (empty pass contributions — recipes
// drive graph assembly, not a parallel pass planner fork).
[[nodiscard]] ShaderFeatureRegistry
make_paper_cinematic_lighting_feature_registry();

// Build a shared RenderGraphDesc: shadow buffer → main color → optional veil.
// No style-package name appears in pass keys or resource tags.
[[nodiscard]] std::expected<RenderGraphDesc, diagnostics::Error>
build_paper_cinematic_shot_graph(const PaperCinematicShotGraphRequest &request);

[[nodiscard]] std::string format_paper_cinematic_shot_graph_manifest(
    const PaperCinematicShotGraphRequest &request, const RenderGraphDesc &graph,
    const CompiledRenderGraph &compiled);

} // namespace instantee::render_core
