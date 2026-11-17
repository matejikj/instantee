#include <instantee/render_core/primitives.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace instantee::render_core {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "primitives", message};
}

[[nodiscard]] float clampf(float value, float lo, float hi) noexcept {
  return std::max(lo, std::min(hi, value));
}

void blend_pixel(std::uint8_t *dst, Rgba8 src, float coverage) noexcept {
  coverage = clampf(coverage, 0.0f, 1.0f);
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
  if (sa <= 0.0f) {
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

[[nodiscard]] float cover_rect(float px, float py,
                               const Rect2f &rect) noexcept {
  return (px >= rect.x && py >= rect.y && px < rect.x + rect.width &&
          py < rect.y + rect.height)
             ? 1.0f
             : 0.0f;
}

[[nodiscard]] float cover_ellipse(float px, float py,
                                  const Rect2f &rect) noexcept {
  if (rect.width <= 0.0f || rect.height <= 0.0f) {
    return 0.0f;
  }
  const float cx = rect.x + rect.width * 0.5f;
  const float cy = rect.y + rect.height * 0.5f;
  const float rx = rect.width * 0.5f;
  const float ry = rect.height * 0.5f;
  const float nx = (px + 0.5f - cx) / rx;
  const float ny = (py + 0.5f - cy) / ry;
  return (nx * nx + ny * ny) <= 1.0f ? 1.0f : 0.0f;
}

[[nodiscard]] float sd_rounded_rect(float px, float py, const Rect2f &rect,
                                    float radius) noexcept {
  const float cx = rect.x + rect.width * 0.5f;
  const float cy = rect.y + rect.height * 0.5f;
  const float half_w = rect.width * 0.5f;
  const float half_h = rect.height * 0.5f;
  const float r = clampf(radius, 0.0f, std::min(half_w, half_h));
  const float qx = std::abs(px + 0.5f - cx) - (half_w - r);
  const float qy = std::abs(py + 0.5f - cy) - (half_h - r);
  const float ox = std::max(qx, 0.0f);
  const float oy = std::max(qy, 0.0f);
  return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

[[nodiscard]] float cover_rounded_rect(float px, float py, const Rect2f &rect,
                                       float radius) noexcept {
  return sd_rounded_rect(px, py, rect, radius) <= 0.0f ? 1.0f : 0.0f;
}

[[nodiscard]] float cover_line(float px, float py, float x0, float y0, float x1,
                               float y1, float stroke_width) noexcept {
  const float half = std::max(0.5f, stroke_width * 0.5f);
  const float vx = x1 - x0;
  const float vy = y1 - y0;
  const float len2 = vx * vx + vy * vy;
  const float cx = px + 0.5f;
  const float cy = py + 0.5f;
  float t = 0.0f;
  if (len2 > 0.0f) {
    t = clampf(((cx - x0) * vx + (cy - y0) * vy) / len2, 0.0f, 1.0f);
  }
  const float dx = cx - (x0 + vx * t);
  const float dy = cy - (y0 + vy * t);
  return (dx * dx + dy * dy) <= (half * half) ? 1.0f : 0.0f;
}

void visit_bounds(Rgba8Image &image, float x0, float y0, float x1, float y1,
                  Rgba8 color, const auto &coverage_at) {
  if (image.width_px == 0 || image.height_px == 0 || image.rgba.empty()) {
    return;
  }
  const std::int64_t ix0 =
      std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(x0)));
  const std::int64_t iy0 =
      std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(y0)));
  const std::int64_t ix1 =
      std::min<std::int64_t>(static_cast<std::int64_t>(image.width_px),
                             static_cast<std::int64_t>(std::ceil(x1)));
  const std::int64_t iy1 =
      std::min<std::int64_t>(static_cast<std::int64_t>(image.height_px),
                             static_cast<std::int64_t>(std::ceil(y1)));
  for (std::int64_t y = iy0; y < iy1; ++y) {
    for (std::int64_t x = ix0; x < ix1; ++x) {
      const float cover =
          coverage_at(static_cast<float>(x), static_cast<float>(y));
      if (cover <= 0.0f) {
        continue;
      }
      const std::size_t idx = (static_cast<std::size_t>(y) * image.width_px +
                               static_cast<std::size_t>(x)) *
                              4U;
      blend_pixel(image.rgba.data() + idx, color, cover);
    }
  }
}

[[nodiscard]] float shape_coverage_impl(const DrawOp &op, float px,
                                        float py) noexcept {
  switch (op.kind) {
  case DrawOpKind::fill_rect:
    return is_valid_rect(op.rect) ? cover_rect(px, py, op.rect) : 0.0f;
  case DrawOpKind::fill_ellipse:
    return is_valid_rect(op.rect) ? cover_ellipse(px, py, op.rect) : 0.0f;
  case DrawOpKind::fill_rounded_rect:
    return is_valid_rect(op.rect)
               ? cover_rounded_rect(px, py, op.rect, op.corner_radius)
               : 0.0f;
  case DrawOpKind::stroke_line:
    if (!(op.stroke_width > 0.0f) || !std::isfinite(op.stroke_width) ||
        !std::isfinite(op.rect.x) || !std::isfinite(op.rect.y) ||
        !std::isfinite(op.x1) || !std::isfinite(op.y1)) {
      return 0.0f;
    }
    return cover_line(px, py, op.rect.x, op.rect.y, op.x1, op.y1,
                      op.stroke_width);
  case DrawOpKind::clear:
    return 0.0f;
  }
  return 0.0f;
}

} // namespace

float shape_coverage(const DrawOp &op, float px, float py) noexcept {
  return shape_coverage_impl(op, px, py);
}

std::expected<void, diagnostics::Error> apply_draw_op(Rgba8Image &image,
                                                      const DrawOp &op) {
  switch (op.kind) {
  case DrawOpKind::clear:
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
      image.rgba[i] = op.color.r;
      image.rgba[i + 1] = op.color.g;
      image.rgba[i + 2] = op.color.b;
      image.rgba[i + 3] = op.color.a;
    }
    return {};
  case DrawOpKind::fill_rect:
    if (!is_valid_rect(op.rect)) {
      return std::unexpected(make_invalid(
          "invalid_rect", "fill_rect requires positive width and height"));
    }
    visit_bounds(image, op.rect.x, op.rect.y, op.rect.x + op.rect.width,
                 op.rect.y + op.rect.height, op.color, [&](float px, float py) {
                   return cover_rect(px, py, op.rect);
                 });
    return {};
  case DrawOpKind::fill_ellipse:
    if (!is_valid_rect(op.rect)) {
      return std::unexpected(
          make_invalid("invalid_ellipse",
                       "fill_ellipse requires positive width and height"));
    }
    visit_bounds(image, op.rect.x, op.rect.y, op.rect.x + op.rect.width,
                 op.rect.y + op.rect.height, op.color, [&](float px, float py) {
                   return cover_ellipse(px, py, op.rect);
                 });
    return {};
  case DrawOpKind::fill_rounded_rect:
    if (!is_valid_rect(op.rect)) {
      return std::unexpected(
          make_invalid("invalid_rounded_rect",
                       "fill_rounded_rect requires positive width and height"));
    }
    visit_bounds(image, op.rect.x, op.rect.y, op.rect.x + op.rect.width,
                 op.rect.y + op.rect.height, op.color, [&](float px, float py) {
                   return cover_rounded_rect(px, py, op.rect, op.corner_radius);
                 });
    return {};
  case DrawOpKind::stroke_line: {
    if (!(op.stroke_width > 0.0f) || !std::isfinite(op.stroke_width) ||
        !std::isfinite(op.rect.x) || !std::isfinite(op.rect.y) ||
        !std::isfinite(op.x1) || !std::isfinite(op.y1)) {
      return std::unexpected(
          make_invalid("invalid_line",
                       "stroke_line requires finite endpoints and width > 0"));
    }
    const float pad = op.stroke_width * 0.5f + 1.0f;
    const float min_x = std::min(op.rect.x, op.x1) - pad;
    const float min_y = std::min(op.rect.y, op.y1) - pad;
    const float max_x = std::max(op.rect.x, op.x1) + pad;
    const float max_y = std::max(op.rect.y, op.y1) + pad;
    visit_bounds(image, min_x, min_y, max_x, max_y, op.color,
                 [&](float px, float py) {
                   return cover_line(px, py, op.rect.x, op.rect.y, op.x1, op.y1,
                                     op.stroke_width);
                 });
    return {};
  }
  }
  return std::unexpected(
      make_invalid("unsupported_draw_op", "unknown DrawOpKind"));
}

std::expected<void, diagnostics::Error> rasterize_pass(Rgba8Image &image,
                                                       const PassDesc &pass) {
  auto valid = validate_pass(pass);
  if (!valid) {
    return valid;
  }
  if (image.width_px != pass.width_px || image.height_px != pass.height_px) {
    image.width_px = pass.width_px;
    image.height_px = pass.height_px;
    const std::size_t bytes = static_cast<std::size_t>(pass.width_px) *
                              static_cast<std::size_t>(pass.height_px) * 4U;
    if (pass.width_px != 0 && bytes / 4U / pass.width_px != pass.height_px) {
      return std::unexpected(
          make_invalid("image_size_overflow", "pass extent overflows buffer"));
    }
    image.rgba.assign(bytes, 0);
  }
  for (const DrawOp &op : pass.ops) {
    if (auto applied = apply_draw_op(image, op); !applied) {
      return applied;
    }
  }
  return {};
}

} // namespace instantee::render_core
