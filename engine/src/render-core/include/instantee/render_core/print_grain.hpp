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
inline constexpr std::string_view kPrintGrainFeature = "print_grain";
inline constexpr std::string_view kPrintGrainOverlayPass =
    "print_grain_overlay";

struct PrintGrainParams {
  std::uint32_t seed = 1;
  // Grain amplitude in [0, 1]. 0 yields a flat base (still a planned pass when
  // the feature is active — disable via inactive feature set instead).
  float intensity = 0.18f;
  // CMYK-style plate misregistration in pixels (finite, may be negative).
  float registration_x_px = 0.75f;
  float registration_y_px = -0.5f;
  // Expected fraction of pixels that receive a paper defect speck in [0, 1].
  float defect_density = 0.02f;
  // Subtle cool tint used for the misregistered plate (kept near-neutral).
  Rgba8 registration_tint{32, 48, 64, 255};
  // Warm speck color for sparse paper defects.
  Rgba8 defect_color{48, 36, 28, 255};
  Rgba8 base_paper{242, 238, 230, 255};
};

struct PrintGrainPassPlan {
  std::vector<std::string> pass_keys{};
  bool grain_active = false;
};

// Builtins + print_grain. Inactive grain contributes no pass.
[[nodiscard]] ShaderFeatureRegistry make_print_grain_shader_feature_registry();

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_print_grain_params(const PrintGrainParams &params);

[[nodiscard]] std::expected<PrintGrainPassPlan, diagnostics::Error>
plan_print_grain_passes(const ShaderFeatureRegistry &registry,
                        const MaterialDesc &material,
                        const std::vector<std::string> &active_feature_names);

// Deterministic unit noise in [0, 1) shared with the MSL fragment.
[[nodiscard]] float print_grain_unit_noise(std::uint32_t seed, std::uint32_t x,
                                           std::uint32_t y,
                                           std::uint32_t channel) noexcept;

// CPU reference for overlay goldens / contract checks. Product frame loops use
// the offline MSL module — this is not a per-frame raster path.
[[nodiscard]] std::expected<Rgba8Image, diagnostics::Error>
rasterize_print_grain_reference(std::uint32_t width_px, std::uint32_t height_px,
                                const PrintGrainParams &params);

} // namespace instantee::render_core
