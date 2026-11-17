#include <instantee/render_core/print_grain.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace instantee::render_core {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "print_grain", message};
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

[[nodiscard]] std::uint8_t to_u8(float v) noexcept {
  return static_cast<std::uint8_t>(std::lround(clamp01(v) * 255.0f));
}

[[nodiscard]] float channel01(std::uint8_t c) noexcept {
  return static_cast<float>(c) / 255.0f;
}

[[nodiscard]] Rgba8 composite_over(Rgba8 dst, Rgba8 src,
                                   float coverage) noexcept {
  coverage = clamp01(coverage);
  if (coverage <= 0.0f) {
    return dst;
  }
  const float sa = channel01(src.a) * coverage;
  if (sa >= 1.0f) {
    return Rgba8{src.r, src.g, src.b, 255};
  }
  const float da = channel01(dst.a);
  const float out_a = sa + da * (1.0f - sa);
  if (out_a <= 0.0f) {
    return Rgba8{0, 0, 0, 0};
  }
  const float sr = channel01(src.r);
  const float sg = channel01(src.g);
  const float sb = channel01(src.b);
  const float dr = channel01(dst.r);
  const float dg = channel01(dst.g);
  const float db = channel01(dst.b);
  return Rgba8{
      to_u8((sr * sa + dr * da * (1.0f - sa)) / out_a),
      to_u8((sg * sa + dg * da * (1.0f - sa)) / out_a),
      to_u8((sb * sa + db * da * (1.0f - sa)) / out_a),
      to_u8(out_a),
  };
}

[[nodiscard]] Rgba8 sample_base(const PrintGrainParams &params,
                                std::uint32_t width_px, std::uint32_t height_px,
                                float fx, float fy) noexcept {
  // Soft radial vignette on the paper base so registration offsets are visible.
  const float cx = static_cast<float>(width_px) * 0.5f;
  const float cy = static_cast<float>(height_px) * 0.5f;
  const float nx = (fx + 0.5f - cx) / std::max(cx, 1.0f);
  const float ny = (fy + 0.5f - cy) / std::max(cy, 1.0f);
  const float falloff = clamp01(1.0f - 0.18f * (nx * nx + ny * ny));
  return Rgba8{
      to_u8(channel01(params.base_paper.r) * falloff),
      to_u8(channel01(params.base_paper.g) * falloff),
      to_u8(channel01(params.base_paper.b) * falloff),
      params.base_paper.a,
  };
}

} // namespace

float print_grain_unit_noise(std::uint32_t seed, std::uint32_t x,
                             std::uint32_t y, std::uint32_t channel) noexcept {
  return unit_from_hash(
      mix_u32(seed, mix_u32(x, mix_u32(y, channel ^ 0xA5A5A5A5u))));
}

std::expected<void, diagnostics::Error>
validate_print_grain_params(const PrintGrainParams &params) {
  if (!std::isfinite(params.intensity) || params.intensity < 0.0f ||
      params.intensity > 1.0f) {
    return std::unexpected(make_invalid(
        "invalid_intensity", "print_grain intensity must be finite in [0, 1]"));
  }
  if (!std::isfinite(params.registration_x_px) ||
      !std::isfinite(params.registration_y_px)) {
    return std::unexpected(
        make_invalid("invalid_registration",
                     "print_grain registration offsets must be finite"));
  }
  if (!std::isfinite(params.defect_density) || params.defect_density < 0.0f ||
      params.defect_density > 1.0f) {
    return std::unexpected(
        make_invalid("invalid_defect_density",
                     "print_grain defect_density must be finite in [0, 1]"));
  }
  return {};
}

ShaderFeatureRegistry make_print_grain_shader_feature_registry() {
  auto registry = make_builtin_shader_feature_registry();

  ShaderFeatureDesc grain;
  grain.id =
      ShaderFeatureId{.name = std::string{kPrintGrainFeature}, .version = 1};
  grain.params.push_back(ShaderParamDesc{
      .name = "intensity",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 0.18f},
  });
  grain.params.push_back(ShaderParamDesc{
      .name = "registration_x_px",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 0.75f},
  });
  grain.params.push_back(ShaderParamDesc{
      .name = "registration_y_px",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = -0.5f},
  });
  grain.params.push_back(ShaderParamDesc{
      .name = "defect_density",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 0.02f},
  });
  grain.params.push_back(ShaderParamDesc{
      .name = "seed",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 1.0f},
  });
  grain.pass_contributions.push_back(PassContribution{
      .pass_key = std::string{kPrintGrainOverlayPass}, .sort_key = 130});
  grain.capabilities.require_apple7 = true;
  (void)registry.register_feature(std::move(grain));

  return registry;
}

std::expected<PrintGrainPassPlan, diagnostics::Error>
plan_print_grain_passes(const ShaderFeatureRegistry &registry,
                        const MaterialDesc &material,
                        const std::vector<std::string> &active_feature_names) {
  auto planned = registry.plan_passes(material, active_feature_names);
  if (!planned) {
    return std::unexpected(planned.error());
  }
  PrintGrainPassPlan out;
  out.pass_keys = planned->pass_keys;
  out.grain_active =
      std::find(out.pass_keys.begin(), out.pass_keys.end(),
                std::string{kPrintGrainOverlayPass}) != out.pass_keys.end();
  return out;
}

std::expected<Rgba8Image, diagnostics::Error>
rasterize_print_grain_reference(std::uint32_t width_px, std::uint32_t height_px,
                                const PrintGrainParams &params) {
  if (width_px == 0 || height_px == 0) {
    return std::unexpected(make_invalid(
        "invalid_extent", "print_grain reference requires positive extents"));
  }
  if (auto ok = validate_print_grain_params(params); !ok) {
    return std::unexpected(ok.error());
  }

  Rgba8Image image;
  image.width_px = width_px;
  image.height_px = height_px;
  image.rgba.assign(static_cast<std::size_t>(width_px) * height_px * 4U, 0);

  for (std::uint32_t y = 0; y < height_px; ++y) {
    for (std::uint32_t x = 0; x < width_px; ++x) {
      Rgba8 pixel = sample_base(params, width_px, height_px,
                                static_cast<float>(x), static_cast<float>(y));

      // Seeded luminance grain (multiplicative, near-neutral).
      const float n = print_grain_unit_noise(params.seed, x, y, /*channel=*/0);
      const float grain = 1.0f + (n - 0.5f) * 2.0f * params.intensity;
      pixel.r = to_u8(channel01(pixel.r) * grain);
      pixel.g = to_u8(channel01(pixel.g) * grain);
      pixel.b = to_u8(channel01(pixel.b) * grain);

      // Misregistered plate: sample base at an offset and tint lightly.
      const float sx = static_cast<float>(x) + params.registration_x_px;
      const float sy = static_cast<float>(y) + params.registration_y_px;
      const float clamped_x =
          std::clamp(sx, 0.0f, static_cast<float>(width_px - 1));
      const float clamped_y =
          std::clamp(sy, 0.0f, static_cast<float>(height_px - 1));
      const Rgba8 plate =
          sample_base(params, width_px, height_px, clamped_x, clamped_y);
      const float plate_luma = 0.2126f * channel01(plate.r) +
                               0.7152f * channel01(plate.g) +
                               0.0722f * channel01(plate.b);
      const float plate_cover =
          clamp01((1.0f - plate_luma) * 0.22f * (0.35f + params.intensity));
      pixel = composite_over(pixel, params.registration_tint, plate_cover);

      // Sparse paper defects (flecks / fiber marks).
      const float d = print_grain_unit_noise(params.seed, x, y, /*channel=*/7);
      if (d < params.defect_density) {
        const float cover =
            0.35f + 0.45f * print_grain_unit_noise(params.seed, x, y, 8);
        pixel = composite_over(pixel, params.defect_color, cover);
      }

      const std::size_t idx = (static_cast<std::size_t>(y) * width_px + x) * 4U;
      image.rgba[idx] = pixel.r;
      image.rgba[idx + 1] = pixel.g;
      image.rgba[idx + 2] = pixel.b;
      image.rgba[idx + 3] = pixel.a;
    }
  }
  return image;
}

} // namespace instantee::render_core
