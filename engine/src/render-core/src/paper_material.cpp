#include <instantee/render_core/paper_material.hpp>

#include <cmath>
#include <string>

namespace instantee::render_core {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "paper_material", message};
}

[[nodiscard]] bool in_range(float value, float min_v, float max_v) noexcept {
  return std::isfinite(value) && value >= min_v && value <= max_v;
}

[[nodiscard]] bool is_known_param(std::string_view name) noexcept {
  return name == kPaperParamFiber || name == kPaperParamRoughness ||
         name == kPaperParamColorWash || name == kPaperParamEdgeThickness ||
         name == kPaperParamShadowBias || name == kPaperParamNormalVariation ||
         name == kPaperParamSeed;
}

} // namespace

PaperMaterialRanges paper_material_ranges_v1() noexcept { return {}; }

PaperMaterialParams paper_material_defaults_v1() noexcept { return {}; }

std::expected<void, diagnostics::Error>
validate_paper_material_params(const PaperMaterialParams &params,
                               const PaperMaterialRanges &ranges) {
  if (!in_range(params.fiber, ranges.fiber_min, ranges.fiber_max)) {
    return std::unexpected(make_invalid("paper_material.fiber_range",
                                        "fiber must be finite within range"));
  }
  if (!in_range(params.roughness, ranges.roughness_min, ranges.roughness_max)) {
    return std::unexpected(
        make_invalid("paper_material.roughness_range",
                     "roughness must be finite within range"));
  }
  if (!in_range(params.color_wash, ranges.color_wash_min,
                ranges.color_wash_max)) {
    return std::unexpected(
        make_invalid("paper_material.color_wash_range",
                     "color_wash must be finite within range"));
  }
  if (!in_range(params.edge_thickness, ranges.edge_thickness_min,
                ranges.edge_thickness_max)) {
    return std::unexpected(
        make_invalid("paper_material.edge_thickness_range",
                     "edge_thickness must be finite within range"));
  }
  if (!in_range(params.shadow_bias, ranges.shadow_bias_min,
                ranges.shadow_bias_max)) {
    return std::unexpected(
        make_invalid("paper_material.shadow_bias_range",
                     "shadow_bias must be finite within range"));
  }
  if (!in_range(params.normal_variation, ranges.normal_variation_min,
                ranges.normal_variation_max)) {
    return std::unexpected(
        make_invalid("paper_material.normal_variation_range",
                     "normal_variation must be finite within range"));
  }
  const double seed = static_cast<double>(params.seed);
  if (!std::isfinite(seed) || seed < ranges.seed_min ||
      seed > ranges.seed_max) {
    return std::unexpected(make_invalid("paper_material.seed_range",
                                        "seed must be within uint32 range"));
  }
  return {};
}

std::expected<PaperMaterialParams, diagnostics::Error>
parse_paper_material_params(
    const std::unordered_map<std::string, double> &authored,
    const PaperMaterialParams &defaults, const PaperMaterialRanges &ranges) {
  PaperMaterialParams out = defaults;
  for (const auto &[key, value] : authored) {
    if (!is_known_param(key)) {
      return std::unexpected(
          make_invalid("paper_material.unknown_param",
                       "unknown paper material parameter: " + key));
    }
    if (!std::isfinite(value)) {
      return std::unexpected(
          make_invalid("paper_material.non_finite",
                       "paper material parameter must be finite: " + key));
    }
    if (key == kPaperParamFiber) {
      out.fiber = static_cast<float>(value);
    } else if (key == kPaperParamRoughness) {
      out.roughness = static_cast<float>(value);
    } else if (key == kPaperParamColorWash) {
      out.color_wash = static_cast<float>(value);
    } else if (key == kPaperParamEdgeThickness) {
      out.edge_thickness = static_cast<float>(value);
    } else if (key == kPaperParamShadowBias) {
      out.shadow_bias = static_cast<float>(value);
    } else if (key == kPaperParamNormalVariation) {
      out.normal_variation = static_cast<float>(value);
    } else if (key == kPaperParamSeed) {
      if (value < ranges.seed_min || value > ranges.seed_max ||
          value != std::floor(value)) {
        return std::unexpected(
            make_invalid("paper_material.seed_range",
                         "seed must be an integer within uint32 range"));
      }
      out.seed = static_cast<std::uint32_t>(value);
    }
  }
  if (auto ok = validate_paper_material_params(out, ranges); !ok) {
    return std::unexpected(ok.error());
  }
  return out;
}

std::vector<ShaderParamDesc>
paper_material_shader_param_descs(const PaperMaterialParams &defaults) {
  auto scalar = [](std::string_view name, float value) {
    return ShaderParamDesc{
        .name = std::string{name},
        .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                            .scalar_f32 = value},
    };
  };
  return {
      scalar(kPaperParamFiber, defaults.fiber),
      scalar(kPaperParamRoughness, defaults.roughness),
      scalar(kPaperParamColorWash, defaults.color_wash),
      scalar(kPaperParamEdgeThickness, defaults.edge_thickness),
      scalar(kPaperParamShadowBias, defaults.shadow_bias),
      scalar(kPaperParamNormalVariation, defaults.normal_variation),
      scalar(kPaperParamSeed, static_cast<float>(defaults.seed)),
  };
}

PaperSheetParams
paper_material_to_sheet_params(const PaperMaterialParams &params,
                               float edge_thickness_px_scale) noexcept {
  PaperSheetParams out;
  out.fiber = params.fiber;
  out.roughness = params.roughness;
  out.color_wash = params.color_wash;
  out.shadow_bias = params.shadow_bias;
  out.seed = params.seed;
  const float scale =
      std::isfinite(edge_thickness_px_scale) && edge_thickness_px_scale > 0.0f
          ? edge_thickness_px_scale
          : 32.0f;
  out.edge_thickness_px = params.edge_thickness * scale;
  return out;
}

} // namespace instantee::render_core
