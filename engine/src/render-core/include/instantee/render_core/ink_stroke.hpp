#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/shader_features.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::render_core {

// GPU ink/pencil stroke reveal. Mesh construction lives in geometry; this
// module only registers the shader feature and validates draw uniforms so
// render-core never depends on path types or style package names.
inline constexpr std::string_view kInkStrokeFeature = "ink_stroke";
inline constexpr std::string_view kInkStrokePass = "ink_stroke_draw";

struct InkStrokeShaderParams {
  // Arc-length reveal in [0, 1]. Animation changes this uniform only — no
  // CPU bitmap restamp.
  float progress = 1.0f;
  float brush_grain = 0.35f;
  std::uint32_t brush_seed = 1;
  Rgba8 ink{20, 40, 90, 255};
};

struct InkStrokePassPlan {
  std::vector<std::string> pass_keys{};
  bool ink_stroke_active = false;
};

[[nodiscard]] ShaderFeatureRegistry make_ink_stroke_shader_feature_registry();

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_ink_stroke_params(const InkStrokeShaderParams &params);

[[nodiscard]] std::expected<InkStrokePassPlan, diagnostics::Error>
plan_ink_stroke_passes(const ShaderFeatureRegistry &registry,
                       const MaterialDesc &material,
                       const std::vector<std::string> &active_feature_names);

} // namespace instantee::render_core
