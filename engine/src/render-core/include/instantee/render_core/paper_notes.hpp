#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/render_graph.hpp>
#include <instantee/render_core/shader_features.hpp>
#include <instantee/render_core/shadow.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace instantee::render_core {

// Shared paper-sheet feature id used by notebook note cards and later by Paper
// Cinema. Render-core never branches on style package names.
inline constexpr std::string_view kPaperSheetFeature = "paper-sheet";
inline constexpr std::string_view kPaperSheetPass = "paper_sheet";

// Bounded paper material knobs. Paper Cinema extends the same feature rather
// than introducing a parallel paper shading path.
struct PaperSheetParams {
  // Fiber / roughness / wash in [0, 1]. Edge thickness and shadow bias finite ≥
  // 0.
  float fiber = 0.35f;
  float roughness = 0.25f;
  float color_wash = 0.12f;
  float edge_thickness_px = 1.25f;
  float shadow_bias = 0.55f; // scales shadow offset/alpha with attach motion
  std::uint32_t seed = 1;
  Rgba8 base_color{250, 245, 230, 255};
  Rgba8 wash_color{255, 252, 240, 255};
  Rgba8 edge_color{70, 55, 40, 90};
};

// Stick / uncover motion. attach=0 peeled away; uncover=0 content fully masked.
struct PaperNoteMotion {
  float attach = 1.0f;
  float uncover = 1.0f;
  float peel_lift_x_px = 0.0f;
  float peel_lift_y_px = -14.0f;
};

struct StickyNoteSpec {
  std::string id{};
  Rect2f bounds{};
  float corner_radius = 5.0f;
  PaperSheetParams paper{};
  ShadowParams shadow{.enabled = true,
                      .soft_radius_px = 0.0f,
                      .offset_x_px = 3.0f,
                      .offset_y_px = 4.0f,
                      .color = Rgba8{0, 0, 0, 72}};
  PaperNoteMotion motion{};
};

struct IndexCardSpec {
  std::string id{};
  Rect2f bounds{};
  float corner_radius = 2.0f;
  bool ruled_lines = true;
  float rule_spacing_px = 10.0f;
  Rgba8 rule_color{170, 185, 210, 110};
  PaperSheetParams paper{.base_color = Rgba8{248, 248, 245, 255},
                         .wash_color = Rgba8{255, 255, 252, 255}};
  ShadowParams shadow{.enabled = true,
                      .soft_radius_px = 0.0f,
                      .offset_x_px = 2.0f,
                      .offset_y_px = 3.0f,
                      .color = Rgba8{0, 0, 0, 64}};
  PaperNoteMotion motion{};
};

struct PhotoMountSpec {
  std::string id{};
  Rect2f mount_bounds{};
  Rect2f photo_frame{};
  float mat_corner_radius = 3.0f;
  float photo_corner_radius = 1.0f;
  Rgba8 photo_color{150, 155, 165, 255};
  PaperSheetParams paper{.edge_thickness_px = 1.5f,
                         .base_color = Rgba8{245, 242, 235, 255},
                         .wash_color = Rgba8{252, 250, 245, 255}};
  ShadowParams shadow{.enabled = true,
                      .soft_radius_px = 0.0f,
                      .offset_x_px = 4.0f,
                      .offset_y_px = 5.0f,
                      .color = Rgba8{0, 0, 0, 80}};
  PaperNoteMotion motion{};
};

using PaperNoteSpec =
    std::variant<StickyNoteSpec, IndexCardSpec, PhotoMountSpec>;

struct PaperNoteScene {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  Rgba8 clear_color{255, 255, 255, 255};
  std::vector<PaperNoteSpec> notes{};
};

struct PaperNotePassPlan {
  std::vector<std::string> pass_keys{};
  bool paper_sheet_active = false;
  bool shadow_active = false;
};

[[nodiscard]] constexpr std::string_view
paper_note_kind_id(const PaperNoteSpec &spec) noexcept {
  if (std::holds_alternative<StickyNoteSpec>(spec)) {
    return "sticky_note";
  }
  if (std::holds_alternative<IndexCardSpec>(spec)) {
    return "index_card";
  }
  return "photo_mount";
}

// Builtins + paper-sheet + drop_shadow + soft_shadow (shared with Paper Cinema
// / existing shadow kit). Inactive features contribute no passes.
[[nodiscard]] ShaderFeatureRegistry make_paper_note_shader_feature_registry();

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_paper_sheet_params(const PaperSheetParams &params);

[[nodiscard]] std::expected<PaperNotePassPlan, diagnostics::Error>
plan_paper_note_passes(const ShaderFeatureRegistry &registry,
                       const MaterialDesc &material,
                       const std::vector<std::string> &active_feature_names);

// Lower note cards onto shared DrawOps (fill/stroke + optional hard shadow).
[[nodiscard]] std::expected<PassDesc, diagnostics::Error>
compose_paper_note_pass(const PaperNoteScene &scene);

[[nodiscard]] std::expected<Rgba8Image, diagnostics::Error>
rasterize_paper_note_scene(const PaperNoteScene &scene);

// Deterministic text dump for note-card goldens.
[[nodiscard]] std::string format_paper_note_golden(std::string_view label,
                                                   const PaperNoteScene &scene,
                                                   const PassDesc &pass,
                                                   const Rgba8Image &img);

} // namespace instantee::render_core
