#include <instantee/render_core/shadow.hpp>

#include <instantee/render_core/primitives.hpp>
#include <instantee/render_core/renderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace instantee::render_core {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "shadow", message};
}

[[nodiscard]] bool is_shape(DrawOpKind kind) noexcept {
  return kind == DrawOpKind::fill_rect || kind == DrawOpKind::fill_ellipse ||
         kind == DrawOpKind::fill_rounded_rect ||
         kind == DrawOpKind::stroke_line;
}

void blend_pixel(std::uint8_t *dst, Rgba8 src, float coverage) noexcept {
  coverage = std::max(0.0f, std::min(1.0f, coverage));
  if (coverage <= 0.0f) {
    return;
  }
  const float sa = (static_cast<float>(src.a) / 255.0f) * coverage;
  if (sa >= 1.0f) {
    dst[0] = src.r;
    dst[1] = src.g;
    dst[2] = src.b;
    dst[3] = 255;
    return;
  }
  const float da = static_cast<float>(dst[3]) / 255.0f;
  const float out_a = sa + da * (1.0f - sa);
  if (out_a <= 0.0f) {
    dst[0] = dst[1] = dst[2] = dst[3] = 0;
    return;
  }
  const float sr = static_cast<float>(src.r) / 255.0f;
  const float sg = static_cast<float>(src.g) / 255.0f;
  const float sb = static_cast<float>(src.b) / 255.0f;
  const float dr = static_cast<float>(dst[0]) / 255.0f;
  const float dg = static_cast<float>(dst[1]) / 255.0f;
  const float db = static_cast<float>(dst[2]) / 255.0f;
  dst[0] = static_cast<std::uint8_t>(
      std::lround(((sr * sa + dr * da * (1.0f - sa)) / out_a) * 255.0f));
  dst[1] = static_cast<std::uint8_t>(
      std::lround(((sg * sa + dg * da * (1.0f - sa)) / out_a) * 255.0f));
  dst[2] = static_cast<std::uint8_t>(
      std::lround(((sb * sa + db * da * (1.0f - sa)) / out_a) * 255.0f));
  dst[3] = static_cast<std::uint8_t>(std::lround(out_a * 255.0f));
}

void stamp_shape_mask(std::vector<float> &mask, std::uint32_t width_px,
                      std::uint32_t height_px, const DrawOp &shape,
                      float offset_x, float offset_y) {
  DrawOp shifted = shape;
  shifted.rect.x += offset_x;
  shifted.rect.y += offset_y;
  shifted.x1 += offset_x;
  shifted.y1 += offset_y;
  for (std::uint32_t y = 0; y < height_px; ++y) {
    for (std::uint32_t x = 0; x < width_px; ++x) {
      const float cover =
          shape_coverage(shifted, static_cast<float>(x), static_cast<float>(y));
      if (cover <= 0.0f) {
        continue;
      }
      const std::size_t idx =
          static_cast<std::size_t>(y) * width_px + static_cast<std::size_t>(x);
      mask[idx] = std::max(mask[idx], cover);
    }
  }
}

void box_blur_axis(std::vector<float> &dst, const std::vector<float> &src,
                   std::uint32_t width_px, std::uint32_t height_px,
                   std::int32_t radius, bool horizontal) {
  const std::int32_t w = static_cast<std::int32_t>(width_px);
  const std::int32_t h = static_cast<std::int32_t>(height_px);
  const std::int32_t diam = radius * 2 + 1;
  if (diam <= 1) {
    dst = src;
    return;
  }
  for (std::int32_t y = 0; y < h; ++y) {
    for (std::int32_t x = 0; x < w; ++x) {
      float sum = 0.0f;
      for (std::int32_t k = -radius; k <= radius; ++k) {
        std::int32_t sx = x;
        std::int32_t sy = y;
        if (horizontal) {
          sx = std::clamp(x + k, 0, w - 1);
        } else {
          sy = std::clamp(y + k, 0, h - 1);
        }
        sum += src[static_cast<std::size_t>(sy) * width_px +
                   static_cast<std::size_t>(sx)];
      }
      dst[static_cast<std::size_t>(y) * width_px +
          static_cast<std::size_t>(x)] = sum / static_cast<float>(diam);
    }
  }
}

void separable_box_blur(std::vector<float> &mask, std::uint32_t width_px,
                        std::uint32_t height_px, float radius_px,
                        std::uint32_t iterations) {
  const float clamped = std::max(0.0f, radius_px);
  const auto radius = static_cast<std::int32_t>(std::lround(clamped));
  if (radius <= 0 || iterations == 0) {
    return;
  }
  std::vector<float> temp(mask.size(), 0.0f);
  for (std::uint32_t i = 0; i < iterations; ++i) {
    box_blur_axis(temp, mask, width_px, height_px, radius, /*horizontal=*/true);
    box_blur_axis(mask, temp, width_px, height_px, radius,
                  /*horizontal=*/false);
  }
}

void composite_mask(Rgba8Image &image, const std::vector<float> &mask,
                    Rgba8 color) {
  for (std::uint32_t y = 0; y < image.height_px; ++y) {
    for (std::uint32_t x = 0; x < image.width_px; ++x) {
      const std::size_t mi = static_cast<std::size_t>(y) * image.width_px + x;
      const float cover = mask[mi];
      if (cover <= 0.0f) {
        continue;
      }
      const std::size_t idx = mi * 4U;
      blend_pixel(image.rgba.data() + idx, color, cover);
    }
  }
}

} // namespace

std::uint32_t shadow_blur_iterations(ShadowQualityTier tier) noexcept {
  switch (tier) {
  case ShadowQualityTier::low:
    return 1;
  case ShadowQualityTier::medium:
    return 2;
  case ShadowQualityTier::high:
    return 3;
  }
  return 1;
}

float shadow_blur_radius_px(float requested_radius_px,
                            ShadowQualityTier tier) noexcept {
  if (!(requested_radius_px > 0.0f) || !std::isfinite(requested_radius_px)) {
    return 0.0f;
  }
  float cap = 2.0f;
  switch (tier) {
  case ShadowQualityTier::low:
    cap = 2.0f;
    break;
  case ShadowQualityTier::medium:
    cap = 4.0f;
    break;
  case ShadowQualityTier::high:
    cap = 8.0f;
    break;
  }
  return std::min(requested_radius_px, cap);
}

ShaderFeatureRegistry make_shadow_shader_feature_registry() {
  auto registry = make_builtin_shader_feature_registry();

  ShaderFeatureDesc drop;
  drop.id =
      ShaderFeatureId{.name = std::string{kDropShadowFeature}, .version = 1};
  drop.params.push_back(ShaderParamDesc{
      .name = "offset_x_px",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 4.0f},
  });
  drop.params.push_back(ShaderParamDesc{
      .name = "offset_y_px",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 4.0f},
  });
  drop.pass_contributions.push_back(PassContribution{
      .pass_key = std::string{kShadowCastPass}, .sort_key = 10});
  drop.capabilities.require_apple7 = false;
  (void)registry.register_feature(std::move(drop));

  ShaderFeatureDesc soft;
  soft.id =
      ShaderFeatureId{.name = std::string{kSoftShadowFeature}, .version = 1};
  soft.params.push_back(ShaderParamDesc{
      .name = "soft_radius_px",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 2.0f},
  });
  soft.params.push_back(ShaderParamDesc{
      .name = "quality_tier",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 2.0f},
  });
  soft.pass_contributions.push_back(PassContribution{
      .pass_key = std::string{kShadowCastPass}, .sort_key = 10});
  soft.pass_contributions.push_back(PassContribution{
      .pass_key = std::string{kShadowBlurPass}, .sort_key = 20});
  soft.capabilities.require_apple7 = false;
  (void)registry.register_feature(std::move(soft));

  // Paper capability probe: layered depth / deformable-ready hook. Registered
  // as a generic feature id — not a paper-cinema style branch in render-core.
  ShaderFeatureDesc probe;
  probe.id = ShaderFeatureId{
      .name = std::string{kPaperLayeredDepthProbeFeature}, .version = 1};
  probe.pass_contributions.push_back(PassContribution{
      .pass_key = std::string{kPaperDepthShadowHookPass}, .sort_key = 15});
  probe.capabilities.require_apple7 = false;
  (void)registry.register_feature(std::move(probe));

  return registry;
}

std::expected<ShadowPassPlan, diagnostics::Error>
plan_shadow_passes(const ShaderFeatureRegistry &registry,
                   const MaterialDesc &material,
                   const std::vector<std::string> &active_feature_names) {
  auto planned = registry.plan_passes(material, active_feature_names);
  if (!planned) {
    return std::unexpected(planned.error());
  }

  ShadowPassPlan out;
  out.pass_keys = planned->pass_keys;
  for (const auto &key : out.pass_keys) {
    if (key == kShadowCastPass || key == kShadowBlurPass ||
        key == kPaperDepthShadowHookPass) {
      ++out.shadow_pass_count;
    } else if (key == kMainColorPass) {
      ++out.content_pass_count;
    }
  }
  return out;
}

std::expected<void, diagnostics::Error>
validate_shadow_scene(const ShadowScene &scene) {
  if (scene.width_px == 0 || scene.height_px == 0) {
    return std::unexpected(make_invalid(
        "invalid_shadow_extent", "shadow scene width/height must be > 0"));
  }
  if (scene.casters.empty()) {
    return std::unexpected(
        make_invalid("empty_shadow_scene", "shadow scene has no casters"));
  }
  for (const auto &caster : scene.casters) {
    if (!is_shape(caster.shape.kind)) {
      return std::unexpected(make_invalid(
          "invalid_shadow_caster", "shadow caster must be a shape DrawOp"));
    }
    PassDesc probe;
    probe.width_px = scene.width_px;
    probe.height_px = scene.height_px;
    probe.color_target = 1;
    probe.ops = {caster.shape};
    if (auto valid = validate_pass(probe); !valid) {
      return std::unexpected(valid.error());
    }
    const auto &shadow = caster.shadow;
    if (!shadow.enabled) {
      continue;
    }
    if (!std::isfinite(shadow.offset_x_px) ||
        !std::isfinite(shadow.offset_y_px) ||
        !std::isfinite(shadow.soft_radius_px) || shadow.soft_radius_px < 0.0f) {
      return std::unexpected(make_invalid(
          "invalid_shadow_params",
          "shadow offsets/radius must be finite; radius must be >= 0"));
    }
  }
  return {};
}

std::expected<Rgba8Image, diagnostics::Error>
rasterize_shadow_scene(const ShadowScene &scene) {
  if (auto valid = validate_shadow_scene(scene); !valid) {
    return std::unexpected(valid.error());
  }

  Rgba8Image image;
  image.width_px = scene.width_px;
  image.height_px = scene.height_px;
  image.rgba.assign(static_cast<std::size_t>(scene.width_px) *
                        static_cast<std::size_t>(scene.height_px) * 4U,
                    0);
  DrawOp clear{.kind = DrawOpKind::clear, .color = scene.clear_color};
  if (auto cleared = apply_draw_op(image, clear); !cleared) {
    return std::unexpected(cleared.error());
  }

  // Pass 1..N: shadows underneath, in caster order (does not reorder content).
  for (const auto &caster : scene.casters) {
    if (!caster.shadow.enabled) {
      continue;
    }
    std::vector<float> mask(
        static_cast<std::size_t>(scene.width_px) * scene.height_px, 0.0f);
    stamp_shape_mask(mask, scene.width_px, scene.height_px, caster.shape,
                     caster.shadow.offset_x_px, caster.shadow.offset_y_px);
    if (caster.shadow.soft_radius_px > 0.0f) {
      const float radius = shadow_blur_radius_px(caster.shadow.soft_radius_px,
                                                 caster.shadow.quality);
      const auto iterations = shadow_blur_iterations(caster.shadow.quality);
      separable_box_blur(mask, scene.width_px, scene.height_px, radius,
                         iterations);
    }
    composite_mask(image, mask, caster.shadow.color);
  }

  // Content pass: original caster order, unchanged by shadow presence.
  for (const auto &caster : scene.casters) {
    if (auto drawn = apply_draw_op(image, caster.shape); !drawn) {
      return std::unexpected(drawn.error());
    }
  }
  return image;
}

} // namespace instantee::render_core
