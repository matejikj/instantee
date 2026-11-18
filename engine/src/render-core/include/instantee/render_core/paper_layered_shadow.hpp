#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/render_graph.hpp>
#include <instantee/render_core/shadow.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::render_core {

// Screen-space paper card in a 2.5D stack. depth is a positive depthUnits
// distance in front of the camera (same convention as core-model DepthStage).
// No ray tracing: separation uses offset drop shadows, contact cues, and a
// simple receiver depth map with quality-tier separable blur.
struct PaperLayerCard {
  Rect2f rect{};
  float depth = 1.0f;
  std::uint32_t layer_order = 0;
  Rgba8 fill{245, 238, 224, 255};
};

struct PaperLayeredShadowParams {
  // Base drop offset in pixels (style spacing.shadow_offset scale).
  float offset_x_px = 8.0f;
  float offset_y_px = 8.0f;
  // Extra offset growth per unit depth separation to the receiver.
  float offset_per_depth = 0.75f;
  // Depth delta at/below which the tighter contact cue replaces the drop.
  float contact_depth = 2.0f;
  float contact_spread_px = 1.5f;
  // Soft blur request; also hard-capped by max_soft_spread_px to avoid halos.
  float soft_radius_px = 2.5f;
  float max_soft_spread_px = 4.0f;
  ShadowQualityTier quality = ShadowQualityTier::medium;
  Rgba8 shadow_color{0, 0, 0, 88};
  Rgba8 contact_color{0, 0, 0, 128};
};

struct PaperLayeredShadowScene {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  Rgba8 clear_color{255, 255, 255, 255};
  std::vector<PaperLayerCard> layers{};
  PaperLayeredShadowParams params{};
};

// Per-caster resolved drop used for goldens / debugging.
struct PaperLayerShadowCast {
  std::uint32_t caster_layer_order = 0;
  float caster_depth = 0;
  float receiver_depth = 0;
  float depth_delta = 0;
  bool contact = false;
  float offset_x_px = 0;
  float offset_y_px = 0;
  float soft_radius_px = 0;
  Rgba8 color{0, 0, 0, 0};
};

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_paper_layered_shadow_scene(const PaperLayeredShadowScene &scene);

// Resolve offset / contact / blur for each layer casting onto the next farther
// receiver (or a synthetic ground plane past the farthest card).
[[nodiscard]] std::expected<std::vector<PaperLayerShadowCast>,
                            diagnostics::Error>
resolve_paper_layer_shadow_casts(const PaperLayeredShadowScene &scene);

// CPU golden: clear → stage-floor drops on uncovered background → each sheet
// far-to-near (content, then nearer casters darkening that sheet). Soft blur
// is tier-clamped and clipped to the receiver so soft edges cannot form a
// floating halo outside the receiving sheet.
[[nodiscard]] std::expected<Rgba8Image, diagnostics::Error>
rasterize_paper_layered_shadows(const PaperLayeredShadowScene &scene);

// Deterministic text dump for layered shadow goldens.
[[nodiscard]] std::string
format_paper_layered_shadow_golden(std::string_view label,
                                   const PaperLayeredShadowScene &scene,
                                   const Rgba8Image &image);

} // namespace instantee::render_core
