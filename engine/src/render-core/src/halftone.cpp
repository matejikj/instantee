#include <instantee/render_core/halftone.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace instantee::render_core {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "halftone", message};
}

// Same LCG-style mix used by the MSL fragment for golden parity.
[[nodiscard]] std::uint32_t mix_u32(std::uint32_t a, std::uint32_t b) noexcept {
  std::uint32_t x = a * 1664525u + b + 1013904223u;
  x ^= x >> 16;
  x *= 2246822519u;
  x ^= x >> 13;
  return x;
}

[[nodiscard]] float unit_from_hash(std::uint32_t h) noexcept {
  return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

[[nodiscard]] float clamp01(float v) noexcept {
  return std::min(1.0f, std::max(0.0f, v));
}

[[nodiscard]] bool ellipse_mask(float x, float y, float width,
                                float height) noexcept {
  const float cx = width * 0.5f;
  const float cy = height * 0.5f;
  const float rx = width * 0.42f;
  const float ry = height * 0.42f;
  if (!(rx > 0.0f && ry > 0.0f)) {
    return false;
  }
  const float nx = (x + 0.5f - cx) / rx;
  const float ny = (y + 0.5f - cy) / ry;
  return (nx * nx + ny * ny) <= 1.0f;
}

[[nodiscard]] bool halftone_ink(std::uint32_t x, std::uint32_t y,
                                std::uint32_t width_px, std::uint32_t height_px,
                                const HalftoneParams &params) noexcept {
  if (params.apply_ellipse_mask &&
      !ellipse_mask(static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(width_px),
                    static_cast<float>(height_px))) {
    return false;
  }

  const float cell =
      params.dot_scale / std::max(params.resolution_scale, 1.0e-6f);
  if (!(cell > 0.0f)) {
    return false;
  }
  const float fx = (static_cast<float>(x) + 0.5f) / cell;
  const float fy = (static_cast<float>(y) + 0.5f) / cell;
  const std::int32_t cx = static_cast<std::int32_t>(std::floor(fx));
  const std::int32_t cy = static_cast<std::int32_t>(std::floor(fy));
  const float lx = fx - static_cast<float>(cx);
  const float ly = fy - static_cast<float>(cy);

  float luma_acc = 0.0f;
  const std::uint32_t samples = std::clamp(params.max_samples, 1u, 8u);
  for (std::uint32_t i = 0; i < samples; ++i) {
    const std::uint32_t h = mix_u32(
        params.seed, mix_u32(static_cast<std::uint32_t>(cx),
                             mix_u32(static_cast<std::uint32_t>(cy), i)));
    const float jx = unit_from_hash(h);
    const float jy = unit_from_hash(mix_u32(h, 0x9e3779b9u));
    const float sx = (static_cast<float>(cx) + jx) * cell;
    const float sy = (static_cast<float>(cy) + jy) * cell;
    const std::uint32_t px = static_cast<std::uint32_t>(
        std::clamp(sx, 0.0f, static_cast<float>(width_px - 1)));
    const std::uint32_t py = static_cast<std::uint32_t>(
        std::clamp(sy, 0.0f, static_cast<float>(height_px - 1)));
    luma_acc += halftone_photo_luma(px, py, width_px, height_px);
  }
  const float tone = clamp01(luma_acc / static_cast<float>(samples));
  // Darker tones → denser ink.
  const float coverage = 1.0f - tone;

  if (params.mode == HalftoneMode::lines) {
    const float phase =
        unit_from_hash(mix_u32(params.seed, static_cast<std::uint32_t>(cy)));
    const float band = std::fabs(std::fmod(lx + phase, 1.0f) - 0.5f) * 2.0f;
    return band <= coverage;
  }

  const float dx = lx - 0.5f;
  const float dy = ly - 0.5f;
  const float dist = std::sqrt(dx * dx + dy * dy);
  const float radius = 0.5f * std::sqrt(coverage);
  return dist <= radius;
}

} // namespace

float halftone_photo_luma(std::uint32_t x, std::uint32_t y,
                          std::uint32_t width_px,
                          std::uint32_t height_px) noexcept {
  if (width_px == 0 || height_px == 0) {
    return 0.0f;
  }
  const float u = static_cast<float>(x) / static_cast<float>(width_px);
  const float v = static_cast<float>(y) / static_cast<float>(height_px);
  return clamp01(0.25f + 0.55f * u + 0.20f * (1.0f - v));
}

std::expected<void, diagnostics::Error>
validate_halftone_params(const HalftoneParams &params) {
  if (!std::isfinite(params.dot_scale) || !(params.dot_scale > 0.0f)) {
    return std::unexpected(make_invalid(
        "invalid_dot_scale", "halftone dot_scale must be finite and > 0"));
  }
  if (!std::isfinite(params.resolution_scale) ||
      !(params.resolution_scale > 0.0f) || params.resolution_scale > 1.0f) {
    return std::unexpected(
        make_invalid("invalid_resolution_scale",
                     "halftone resolution_scale must be in (0, 1]"));
  }
  if (params.max_samples == 0 || params.max_samples > 8) {
    return std::unexpected(make_invalid(
        "invalid_max_samples", "halftone max_samples must be in [1, 8]"));
  }
  return {};
}

ShaderFeatureRegistry make_halftone_shader_feature_registry() {
  auto registry = make_builtin_shader_feature_registry();

  ShaderFeatureDesc solid;
  solid.id =
      ShaderFeatureId{.name = std::string{kSolidFillFeature}, .version = 1};
  solid.pass_contributions.push_back(
      PassContribution{.pass_key = "main_color", .sort_key = 100});
  solid.capabilities.require_apple7 = false;
  (void)registry.register_feature(std::move(solid));

  ShaderFeatureDesc halftone;
  halftone.id =
      ShaderFeatureId{.name = std::string{kHalftoneFeature}, .version = 1};
  halftone.params.push_back(ShaderParamDesc{
      .name = "dot_scale",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 1.25f},
  });
  halftone.params.push_back(ShaderParamDesc{
      .name = "resolution_scale",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 1.0f},
  });
  halftone.params.push_back(ShaderParamDesc{
      .name = "seed",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 1.0f},
  });
  halftone.params.push_back(ShaderParamDesc{
      .name = "max_samples",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 4.0f},
  });
  halftone.params.push_back(ShaderParamDesc{
      .name = "mode_dots",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::boolean,
                                          .boolean_value = true},
  });
  halftone.pass_contributions.push_back(PassContribution{
      .pass_key = std::string{kHalftoneOverlayPass}, .sort_key = 120});
  halftone.capabilities.require_apple7 = true;
  (void)registry.register_feature(std::move(halftone));

  return registry;
}

std::expected<HalftonePassPlan, diagnostics::Error>
plan_halftone_passes(const ShaderFeatureRegistry &registry,
                     const MaterialDesc &material,
                     const std::vector<std::string> &active_feature_names) {
  auto planned = registry.plan_passes(material, active_feature_names);
  if (!planned) {
    return std::unexpected(planned.error());
  }
  HalftonePassPlan out;
  out.pass_keys = planned->pass_keys;
  out.halftone_active =
      std::find(out.pass_keys.begin(), out.pass_keys.end(),
                std::string{kHalftoneOverlayPass}) != out.pass_keys.end();
  return out;
}

std::expected<Rgba8Image, diagnostics::Error>
rasterize_halftone_reference(std::uint32_t width_px, std::uint32_t height_px,
                             const HalftoneParams &params) {
  if (width_px == 0 || height_px == 0) {
    return std::unexpected(make_invalid(
        "invalid_extent", "halftone reference requires positive extents"));
  }
  if (auto ok = validate_halftone_params(params); !ok) {
    return std::unexpected(ok.error());
  }

  Rgba8Image image;
  image.width_px = width_px;
  image.height_px = height_px;
  image.rgba.assign(static_cast<std::size_t>(width_px) * height_px * 4U, 0);
  for (std::uint32_t y = 0; y < height_px; ++y) {
    for (std::uint32_t x = 0; x < width_px; ++x) {
      const bool ink = halftone_ink(x, y, width_px, height_px, params);
      const Rgba8 &c = ink ? params.ink : params.paper;
      const std::size_t idx = (static_cast<std::size_t>(y) * width_px + x) * 4U;
      image.rgba[idx] = c.r;
      image.rgba[idx + 1] = c.g;
      image.rgba[idx + 2] = c.b;
      image.rgba[idx + 3] = c.a;
    }
  }
  return image;
}

} // namespace instantee::render_core
