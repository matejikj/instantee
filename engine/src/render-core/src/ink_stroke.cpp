#include <instantee/render_core/ink_stroke.hpp>

#include <cmath>

namespace instantee::render_core {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "render-core", message};
}

} // namespace

ShaderFeatureRegistry make_ink_stroke_shader_feature_registry() {
  auto registry = make_builtin_shader_feature_registry();

  ShaderFeatureDesc ink;
  ink.id =
      ShaderFeatureId{.name = std::string{kInkStrokeFeature}, .version = 1};
  ink.params.push_back(ShaderParamDesc{
      .name = "progress",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 1.0f},
  });
  ink.params.push_back(ShaderParamDesc{
      .name = "brush_grain",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 0.35f},
  });
  ink.params.push_back(ShaderParamDesc{
      .name = "brush_seed",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 1.0f},
  });
  ink.params.push_back(ShaderParamDesc{
      .name = "ink",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::color_rgba,
                                          .color_rgba = Rgba8{20, 40, 90, 255}},
  });
  ink.pass_contributions.push_back(PassContribution{
      .pass_key = std::string{kInkStrokePass}, .sort_key = 80});
  ink.capabilities.require_apple7 = true;
  (void)registry.register_feature(std::move(ink));
  return registry;
}

std::expected<void, diagnostics::Error>
validate_ink_stroke_params(const InkStrokeShaderParams &params) {
  if (!std::isfinite(params.progress)) {
    return std::unexpected(
        make_invalid("ink_stroke.bad_progress", "progress must be finite"));
  }
  if (!std::isfinite(params.brush_grain) || params.brush_grain < 0.0f ||
      params.brush_grain > 1.0f) {
    return std::unexpected(make_invalid(
        "ink_stroke.bad_grain", "brush_grain must be finite in [0, 1]"));
  }
  return {};
}

std::expected<InkStrokePassPlan, diagnostics::Error>
plan_ink_stroke_passes(const ShaderFeatureRegistry &registry,
                       const MaterialDesc &material,
                       const std::vector<std::string> &active_feature_names) {
  auto planned = registry.plan_passes(material, active_feature_names);
  if (!planned) {
    return std::unexpected(planned.error());
  }
  InkStrokePassPlan out;
  out.pass_keys = std::move(planned->pass_keys);
  for (const auto &key : out.pass_keys) {
    if (key == kInkStrokePass) {
      out.ink_stroke_active = true;
      break;
    }
  }
  return out;
}

} // namespace instantee::render_core
