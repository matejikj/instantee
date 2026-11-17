#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/render_graph.hpp>
#include <instantee/render_core/shader_features.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::render_core {

// Stable feature / pass ids. Styles supply parameters only — render-core never
// branches on editorial / paper-cinema package names.
inline constexpr std::string_view kHalftoneFeature = "halftone";
inline constexpr std::string_view kSolidFillFeature = "solid-fill";
inline constexpr std::string_view kHalftoneOverlayPass = "halftone_overlay";

enum class HalftoneMode : std::uint8_t {
  dots = 1,
  lines = 2,
};

struct HalftoneParams {
  HalftoneMode mode = HalftoneMode::dots;
  // Cell size in pixels before resolution_scale. Must be finite and > 0.
  float dot_scale = 1.0f;
  // 1 = full resolution cells; values in (0, 1] coarsen the grid.
  float resolution_scale = 1.0f;
  std::uint32_t seed = 1;
  // Hard cap on tone samples per cell (clamped to [1, 8]).
  std::uint32_t max_samples = 4;
  Rgba8 ink{0, 0, 0, 255};
  Rgba8 paper{255, 255, 255, 255};
  // When true, tone is gated by an ellipse mask covering the canvas.
  bool apply_ellipse_mask = false;
};

struct HalftonePassPlan {
  std::vector<std::string> pass_keys{};
  bool halftone_active = false;
};

[[nodiscard]] constexpr std::string_view
halftone_mode_id(HalftoneMode mode) noexcept {
  switch (mode) {
  case HalftoneMode::dots:
    return "dots";
  case HalftoneMode::lines:
    return "lines";
  }
  return "unknown";
}

// Builtins + solid-fill + halftone. Inactive halftone contributes no pass.
[[nodiscard]] ShaderFeatureRegistry make_halftone_shader_feature_registry();

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_halftone_params(const HalftoneParams &params);

[[nodiscard]] std::expected<HalftonePassPlan, diagnostics::Error>
plan_halftone_passes(const ShaderFeatureRegistry &registry,
                     const MaterialDesc &material,
                     const std::vector<std::string> &active_feature_names);

// Deterministic synthetic photo luminance in [0, 1] (shared with MSL oracle).
[[nodiscard]] float halftone_photo_luma(std::uint32_t x, std::uint32_t y,
                                        std::uint32_t width_px,
                                        std::uint32_t height_px) noexcept;

// CPU reference for goldens / contract checks. Product frame loops use the
// offline MSL module — this is not a per-frame raster path.
[[nodiscard]] std::expected<Rgba8Image, diagnostics::Error>
rasterize_halftone_reference(std::uint32_t width_px, std::uint32_t height_px,
                             const HalftoneParams &params);

} // namespace instantee::render_core
