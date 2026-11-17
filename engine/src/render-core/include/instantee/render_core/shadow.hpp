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

// Screen-space shadow quality. Soft shadows use separable box blur; the tier
// only changes blur iteration count / radius clamp — never content draw order.
enum class ShadowQualityTier : std::uint8_t {
  low = 1,
  medium = 2,
  high = 3,
};

struct ShadowParams {
  bool enabled = false;
  // Soft when soft_radius_px > 0; otherwise a hard offset silhouette.
  float soft_radius_px = 0.0f;
  float offset_x_px = 4.0f;
  float offset_y_px = 4.0f;
  Rgba8 color{0, 0, 0, 96};
  ShadowQualityTier quality = ShadowQualityTier::medium;
};

// One content layer that may cast a shadow. Content ops keep source order;
// shadows are planned as earlier passes underneath.
struct ShadowCaster {
  DrawOp shape{};
  ShadowParams shadow{};
};

struct ShadowScene {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  Rgba8 clear_color{0, 0, 0, 255};
  std::vector<ShadowCaster> casters{};
};

struct ShadowPassPlan {
  // Ordered logical pass keys. Empty when every caster has shadow disabled.
  std::vector<std::string> pass_keys{};
  std::uint32_t shadow_pass_count = 0;
  std::uint32_t content_pass_count = 0;
};

// Stable feature ids (not style package names).
inline constexpr std::string_view kDropShadowFeature = "drop_shadow";
inline constexpr std::string_view kSoftShadowFeature = "soft_shadow";
inline constexpr std::string_view kPaperLayeredDepthProbeFeature =
    "paper_layered_depth_probe";

inline constexpr std::string_view kShadowCastPass = "shadow_cast";
inline constexpr std::string_view kShadowBlurPass = "shadow_blur";
inline constexpr std::string_view kMainColorPass = "main_color";
inline constexpr std::string_view kPaperDepthShadowHookPass =
    "paper_depth_shadow_hook";

// Registers drop_shadow, soft_shadow, paper_layered_depth_probe, and keeps the
// existing builtins. Paper probe is a capability hook, not a style fork.
[[nodiscard]] ShaderFeatureRegistry make_shadow_shader_feature_registry();

// Plans passes for a material/active set. Shadows sort before main_color so
// content blending order is unchanged. Inactive shadow features add nothing.
[[nodiscard]] std::expected<ShadowPassPlan, diagnostics::Error>
plan_shadow_passes(const ShaderFeatureRegistry &registry,
                   const MaterialDesc &material,
                   const std::vector<std::string> &active_feature_names);

// Separable box-blur iteration count for a soft-shadow tier.
[[nodiscard]] std::uint32_t
shadow_blur_iterations(ShadowQualityTier tier) noexcept;

// Clamped blur radius for a tier (pixels).
[[nodiscard]] float shadow_blur_radius_px(float requested_radius_px,
                                          ShadowQualityTier tier) noexcept;

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_shadow_scene(const ShadowScene &scene);

// CPU golden: clear → per-caster shadow (hard or soft) → content in caster
// order. Soft path is screen-space separable blur of the offset silhouette.
[[nodiscard]] std::expected<Rgba8Image, diagnostics::Error>
rasterize_shadow_scene(const ShadowScene &scene);

} // namespace instantee::render_core
