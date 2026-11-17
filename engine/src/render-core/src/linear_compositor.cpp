#include <instantee/render_core/linear_compositor.hpp>

#include <instantee/render_core/primitives.hpp>
#include <instantee/render_core/renderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace instantee::render_core {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "linear_compositor", message};
}

[[nodiscard]] bool is_shape(DrawOpKind kind) noexcept {
  return kind == DrawOpKind::fill_rect || kind == DrawOpKind::fill_ellipse ||
         kind == DrawOpKind::fill_rounded_rect ||
         kind == DrawOpKind::stroke_line;
}

} // namespace

core::LinearRgba srgb8_straight_to_linear_premul(Rgba8 color) noexcept {
  const double a = static_cast<double>(color.a) / 255.0;
  const core::LinearRgba linear =
      core::srgb_rgba_to_linear(static_cast<double>(color.r) / 255.0,
                                static_cast<double>(color.g) / 255.0,
                                static_cast<double>(color.b) / 255.0, a);
  return core::LinearRgba{linear.r * linear.a, linear.g * linear.a,
                          linear.b * linear.a, linear.a};
}

void blend_linear_premul(core::LinearRgba &dst, core::LinearRgba src) noexcept {
  const double inv = 1.0 - std::clamp(src.a, 0.0, 1.0);
  dst.r = src.r + dst.r * inv;
  dst.g = src.g + dst.g * inv;
  dst.b = src.b + dst.b * inv;
  dst.a = src.a + dst.a * inv;
}

std::expected<void, diagnostics::Error>
clear_linear_premul(LinearPremulImage &image, Rgba8 clear_srgb_straight) {
  if (image.width_px == 0 || image.height_px == 0) {
    return std::unexpected(make_invalid(
        "invalid_linear_extent", "linear image width/height must be > 0"));
  }
  const std::size_t count = static_cast<std::size_t>(image.width_px) *
                            static_cast<std::size_t>(image.height_px) * 4U;
  if (image.rgba.size() != count) {
    image.rgba.assign(count, 0.0f);
  }
  const core::LinearRgba clear =
      srgb8_straight_to_linear_premul(clear_srgb_straight);
  for (std::size_t i = 0; i < count; i += 4) {
    image.rgba[i] = static_cast<float>(clear.r);
    image.rgba[i + 1] = static_cast<float>(clear.g);
    image.rgba[i + 2] = static_cast<float>(clear.b);
    image.rgba[i + 3] = static_cast<float>(clear.a);
  }
  return {};
}

std::expected<void, diagnostics::Error>
composite_linear_layer(LinearPremulImage &image, const LinearLayer &layer) {
  if (image.width_px == 0 || image.height_px == 0 ||
      image.rgba.size() !=
          static_cast<std::size_t>(image.width_px) * image.height_px * 4U) {
    return std::unexpected(make_invalid(
        "invalid_linear_buffer", "linear image buffer is not initialized"));
  }
  if (!is_shape(layer.shape.kind)) {
    return std::unexpected(make_invalid(
        "invalid_linear_layer", "linear layer requires a shape DrawOp"));
  }
  if (!std::isfinite(layer.opacity) || layer.opacity < 0.0f ||
      layer.opacity > 1.0f) {
    return std::unexpected(make_invalid(
        "invalid_layer_opacity", "layer opacity must be finite in [0, 1]"));
  }
  PassDesc probe;
  probe.width_px = image.width_px;
  probe.height_px = image.height_px;
  probe.color_target = 1;
  probe.ops = {layer.shape};
  if (auto valid = validate_pass(probe); !valid) {
    return std::unexpected(valid.error());
  }

  core::LinearRgba src = srgb8_straight_to_linear_premul(layer.shape.color);
  src.r *= layer.opacity;
  src.g *= layer.opacity;
  src.b *= layer.opacity;
  src.a *= layer.opacity;

  for (std::uint32_t y = 0; y < image.height_px; ++y) {
    for (std::uint32_t x = 0; x < image.width_px; ++x) {
      const float cover = shape_coverage(layer.shape, static_cast<float>(x),
                                         static_cast<float>(y));
      if (cover <= 0.0f) {
        continue;
      }
      core::LinearRgba frag = src;
      frag.r *= cover;
      frag.g *= cover;
      frag.b *= cover;
      frag.a *= cover;
      const std::size_t idx =
          (static_cast<std::size_t>(y) * image.width_px + x) * 4U;
      core::LinearRgba dst{image.rgba[idx], image.rgba[idx + 1],
                           image.rgba[idx + 2], image.rgba[idx + 3]};
      blend_linear_premul(dst, frag);
      image.rgba[idx] = static_cast<float>(dst.r);
      image.rgba[idx + 1] = static_cast<float>(dst.g);
      image.rgba[idx + 2] = static_cast<float>(dst.b);
      image.rgba[idx + 3] = static_cast<float>(dst.a);
    }
  }
  return {};
}

std::expected<void, diagnostics::Error>
validate_linear_composite_scene(const LinearCompositeScene &scene) {
  if (scene.width_px == 0 || scene.height_px == 0) {
    return std::unexpected(make_invalid(
        "invalid_linear_extent", "composite scene width/height must be > 0"));
  }
  if (scene.layers.empty()) {
    return std::unexpected(make_invalid(
        "empty_linear_scene", "composite scene requires at least one layer"));
  }
  for (const auto &layer : scene.layers) {
    if (!is_shape(layer.shape.kind)) {
      return std::unexpected(make_invalid(
          "invalid_linear_layer", "each layer requires a shape DrawOp"));
    }
    if (!std::isfinite(layer.opacity) || layer.opacity < 0.0f ||
        layer.opacity > 1.0f) {
      return std::unexpected(make_invalid(
          "invalid_layer_opacity", "layer opacity must be finite in [0, 1]"));
    }
    PassDesc probe;
    probe.width_px = scene.width_px;
    probe.height_px = scene.height_px;
    probe.color_target = 1;
    probe.ops = {layer.shape};
    if (auto valid = validate_pass(probe); !valid) {
      return std::unexpected(valid.error());
    }
  }
  return {};
}

std::expected<LinearPremulImage, diagnostics::Error>
rasterize_linear_composite(const LinearCompositeScene &scene) {
  if (auto valid = validate_linear_composite_scene(scene); !valid) {
    return std::unexpected(valid.error());
  }
  LinearPremulImage image;
  image.width_px = scene.width_px;
  image.height_px = scene.height_px;
  image.rgba.assign(static_cast<std::size_t>(scene.width_px) *
                        static_cast<std::size_t>(scene.height_px) * 4U,
                    0.0f);
  if (auto cleared = clear_linear_premul(image, scene.clear_color); !cleared) {
    return std::unexpected(cleared.error());
  }
  for (const auto &layer : scene.layers) {
    if (auto drawn = composite_linear_layer(image, layer); !drawn) {
      return std::unexpected(drawn.error());
    }
  }
  return image;
}

std::expected<Rgba8Image, diagnostics::Error>
encode_rec709_rgba8(const LinearPremulImage &image) {
  if (image.width_px == 0 || image.height_px == 0) {
    return std::unexpected(make_invalid(
        "invalid_linear_extent", "linear image width/height must be > 0"));
  }
  const std::size_t count = static_cast<std::size_t>(image.width_px) *
                            static_cast<std::size_t>(image.height_px) * 4U;
  if (image.rgba.size() != count) {
    return std::unexpected(make_invalid("invalid_linear_buffer",
                                        "linear image buffer size mismatch"));
  }

  Rgba8Image out;
  out.width_px = image.width_px;
  out.height_px = image.height_px;
  out.rgba.resize(count);
  for (std::size_t i = 0; i < count; i += 4) {
    const double pr = image.rgba[i];
    const double pg = image.rgba[i + 1];
    const double pb = image.rgba[i + 2];
    const double a =
        std::clamp(static_cast<double>(image.rgba[i + 3]), 0.0, 1.0);
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    if (a > 0.0) {
      r = pr / a;
      g = pg / a;
      b = pb / a;
    }
    out.rgba[i] = static_cast<std::uint8_t>(
        std::lround(core::linear_channel_to_srgb(r) * 255.0));
    out.rgba[i + 1] = static_cast<std::uint8_t>(
        std::lround(core::linear_channel_to_srgb(g) * 255.0));
    out.rgba[i + 2] = static_cast<std::uint8_t>(
        std::lround(core::linear_channel_to_srgb(b) * 255.0));
    out.rgba[i + 3] = static_cast<std::uint8_t>(std::lround(a * 255.0));
  }
  return out;
}

std::expected<std::vector<std::uint8_t>, diagnostics::Error>
flatten_rec709_rgba8_over_black(std::span<const std::uint8_t> straight_rgba) {
  if (straight_rgba.size() % 4U != 0) {
    return std::unexpected(make_invalid(
        "rgba_size_mismatch", "RGBA8 buffer size must be a multiple of 4"));
  }
  std::vector<std::uint8_t> out(straight_rgba.size());
  for (std::size_t i = 0; i < straight_rgba.size(); i += 4) {
    const std::uint16_t a = straight_rgba[i + 3];
    out[i] = static_cast<std::uint8_t>(
        (static_cast<std::uint16_t>(straight_rgba[i]) * a + 127U) / 255U);
    out[i + 1] = static_cast<std::uint8_t>(
        (static_cast<std::uint16_t>(straight_rgba[i + 1]) * a + 127U) / 255U);
    out[i + 2] = static_cast<std::uint8_t>(
        (static_cast<std::uint16_t>(straight_rgba[i + 2]) * a + 127U) / 255U);
    out[i + 3] = 255;
  }
  return out;
}

} // namespace instantee::render_core
