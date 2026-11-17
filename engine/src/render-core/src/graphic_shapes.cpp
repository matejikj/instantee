#include <instantee/render_core/graphic_shapes.hpp>

#include <instantee/render_core/primitives.hpp>
#include <instantee/render_core/renderer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>
#include <utility>

namespace instantee::render_core {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "graphic_shapes", message};
}

[[nodiscard]] bool finite_point(float x, float y) noexcept {
  return std::isfinite(x) && std::isfinite(y);
}

[[nodiscard]] bool finite_positive(float v) noexcept {
  return std::isfinite(v) && v > 0.0f;
}

[[nodiscard]] DrawOp make_line(float x0, float y0, float x1, float y1,
                               float width, Rgba8 color) {
  return DrawOp{.kind = DrawOpKind::stroke_line,
                .rect = {.x = x0, .y = y0, .width = 0, .height = 0},
                .x1 = x1,
                .y1 = y1,
                .color = color,
                .stroke_width = width};
}

[[nodiscard]] DrawOp make_fill_rect(Rect2f bounds, Rgba8 color) {
  return DrawOp{.kind = DrawOpKind::fill_rect, .rect = bounds, .color = color};
}

[[nodiscard]] DrawOp make_fill_rounded(Rect2f bounds, float radius,
                                       Rgba8 color) {
  return DrawOp{.kind = DrawOpKind::fill_rounded_rect,
                .rect = bounds,
                .color = color,
                .corner_radius = radius};
}

[[nodiscard]] DrawOp make_fill_ellipse(Rect2f bounds, Rgba8 color) {
  return DrawOp{
      .kind = DrawOpKind::fill_ellipse, .rect = bounds, .color = color};
}

[[nodiscard]] std::expected<std::vector<DrawOp>, diagnostics::Error>
lower_arrow(const ArrowShape &shape) {
  if (!finite_point(shape.x0, shape.y0) || !finite_point(shape.x1, shape.y1)) {
    return std::unexpected(
        make_invalid("invalid_arrow", "arrow endpoints must be finite"));
  }
  if (!finite_positive(shape.shaft_width) ||
      !finite_positive(shape.head_length) ||
      !finite_positive(shape.head_width)) {
    return std::unexpected(make_invalid(
        "invalid_arrow", "arrow shaft/head sizes must be finite and > 0"));
  }
  const float dx = shape.x1 - shape.x0;
  const float dy = shape.y1 - shape.y0;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (!(len > 1.0e-3f)) {
    return std::unexpected(
        make_invalid("invalid_arrow", "arrow length must be > 0"));
  }
  if (shape.head_length >= len) {
    return std::unexpected(make_invalid(
        "invalid_arrow", "arrow head_length must be shorter than the shaft"));
  }

  const float ux = dx / len;
  const float uy = dy / len;
  const float px = -uy;
  const float py = ux;
  const float base_x = shape.x1 - ux * shape.head_length;
  const float base_y = shape.y1 - uy * shape.head_length;
  const float half = shape.head_width * 0.5f;

  std::vector<DrawOp> ops;
  ops.push_back(make_line(shape.x0, shape.y0, base_x, base_y, shape.shaft_width,
                          shape.color));
  ops.push_back(make_line(shape.x1, shape.y1, base_x + px * half,
                          base_y + py * half, shape.shaft_width, shape.color));
  ops.push_back(make_line(shape.x1, shape.y1, base_x - px * half,
                          base_y - py * half, shape.shaft_width, shape.color));
  return ops;
}

[[nodiscard]] std::expected<std::vector<DrawOp>, diagnostics::Error>
lower_technical_line(const TechnicalLineShape &shape) {
  if (!finite_point(shape.x0, shape.y0) || !finite_point(shape.x1, shape.y1)) {
    return std::unexpected(make_invalid(
        "invalid_technical_line", "technical line endpoints must be finite"));
  }
  if (!finite_positive(shape.stroke_width)) {
    return std::unexpected(make_invalid("invalid_technical_line",
                                        "stroke_width must be finite and > 0"));
  }
  if (!std::isfinite(shape.tick_spacing) || shape.tick_spacing < 0.0f ||
      !std::isfinite(shape.tick_length) || shape.tick_length < 0.0f) {
    return std::unexpected(
        make_invalid("invalid_technical_line",
                     "tick_spacing/length must be finite and >= 0"));
  }

  const float dx = shape.x1 - shape.x0;
  const float dy = shape.y1 - shape.y0;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (!(len > 1.0e-3f)) {
    return std::unexpected(make_invalid("invalid_technical_line",
                                        "technical line length must be > 0"));
  }

  std::vector<DrawOp> ops;
  ops.push_back(make_line(shape.x0, shape.y0, shape.x1, shape.y1,
                          shape.stroke_width, shape.color));

  if (shape.tick_spacing > 0.0f && shape.tick_length > 0.0f) {
    const float ux = dx / len;
    const float uy = dy / len;
    const float px = -uy;
    const float py = ux;
    const float half = shape.tick_length * 0.5f;
    // Inclusive end ticks; cap count to keep lowering bounded.
    const auto max_ticks = static_cast<std::uint32_t>(
        std::min(64.0f, std::floor(len / shape.tick_spacing) + 1.0f));
    for (std::uint32_t i = 0; i < max_ticks; ++i) {
      const float t = std::min(len, static_cast<float>(i) * shape.tick_spacing);
      const float cx = shape.x0 + ux * t;
      const float cy = shape.y0 + uy * t;
      ops.push_back(make_line(cx - px * half, cy - py * half, cx + px * half,
                              cy + py * half, shape.stroke_width, shape.color));
    }
  }
  return ops;
}

[[nodiscard]] std::expected<std::vector<DrawOp>, diagnostics::Error>
lower_outline(const OutlineShape &shape) {
  if (!is_valid_rect(shape.bounds) || !std::isfinite(shape.bounds.x) ||
      !std::isfinite(shape.bounds.y)) {
    return std::unexpected(
        make_invalid("invalid_outline", "outline bounds must be finite > 0"));
  }
  if (!finite_positive(shape.stroke_width)) {
    return std::unexpected(make_invalid(
        "invalid_outline", "outline stroke_width must be finite and > 0"));
  }
  const float x0 = shape.bounds.x;
  const float y0 = shape.bounds.y;
  const float x1 = shape.bounds.x + shape.bounds.width;
  const float y1 = shape.bounds.y + shape.bounds.height;
  std::vector<DrawOp> ops;
  ops.push_back(make_line(x0, y0, x1, y0, shape.stroke_width, shape.color));
  ops.push_back(make_line(x1, y0, x1, y1, shape.stroke_width, shape.color));
  ops.push_back(make_line(x1, y1, x0, y1, shape.stroke_width, shape.color));
  ops.push_back(make_line(x0, y1, x0, y0, shape.stroke_width, shape.color));
  return ops;
}

[[nodiscard]] std::expected<std::vector<DrawOp>, diagnostics::Error>
lower_stamp(const StampShape &shape) {
  if (!is_valid_rect(shape.bounds) || !std::isfinite(shape.bounds.x) ||
      !std::isfinite(shape.bounds.y)) {
    return std::unexpected(
        make_invalid("invalid_stamp", "stamp bounds must be finite > 0"));
  }
  if (!std::isfinite(shape.corner_radius) || shape.corner_radius < 0.0f ||
      !std::isfinite(shape.ring_inset_px) || shape.ring_inset_px < 0.0f) {
    return std::unexpected(make_invalid(
        "invalid_stamp", "corner_radius/ring_inset must be finite and >= 0"));
  }
  if (shape.ring_inset_px * 2.0f >= shape.bounds.width ||
      shape.ring_inset_px * 2.0f >= shape.bounds.height) {
    return std::unexpected(make_invalid(
        "invalid_stamp", "ring_inset must leave a positive inner rect"));
  }

  Rect2f inner = shape.bounds;
  inner.x += shape.ring_inset_px;
  inner.y += shape.ring_inset_px;
  inner.width -= shape.ring_inset_px * 2.0f;
  inner.height -= shape.ring_inset_px * 2.0f;
  const float inner_radius =
      std::max(0.0f, shape.corner_radius - shape.ring_inset_px);

  std::vector<DrawOp> ops;
  ops.push_back(
      make_fill_rounded(shape.bounds, shape.corner_radius, shape.ring));
  ops.push_back(make_fill_rounded(inner, inner_radius, shape.fill));
  // Center seal mark — shared ellipse primitive, not a custom stamp renderer.
  const float seal_w = inner.width * 0.45f;
  const float seal_h = inner.height * 0.45f;
  Rect2f seal{.x = inner.x + (inner.width - seal_w) * 0.5f,
              .y = inner.y + (inner.height - seal_h) * 0.5f,
              .width = seal_w,
              .height = seal_h};
  ops.push_back(make_fill_ellipse(seal, shape.ring));
  return ops;
}

// Classic 7-segment bit masks for digits 0–9 (a,b,c,d,e,f,g).
[[nodiscard]] std::uint8_t seven_seg_mask(std::uint8_t digit) noexcept {
  static constexpr std::array<std::uint8_t, 10> kMasks = {
      0b0111111, // 0 a b c d e f
      0b0000110, // 1 b c
      0b1011011, // 2 a b d e g
      0b1001111, // 3 a b c d g
      0b1100110, // 4 b c f g
      0b1101101, // 5 a c d f g
      0b1111101, // 6 a c d e f g
      0b0000111, // 7 a b c
      0b1111111, // 8
      0b1101111, // 9 a b c d f g
  };
  return kMasks[digit % 10];
}

[[nodiscard]] std::expected<std::vector<DrawOp>, diagnostics::Error>
lower_numeric_callout(const NumericCalloutShape &shape) {
  if (!finite_point(shape.cx, shape.cy) || !finite_positive(shape.radius)) {
    return std::unexpected(make_invalid(
        "invalid_callout", "callout center/radius must be finite; radius > 0"));
  }
  if (shape.digit > 9) {
    return std::unexpected(
        make_invalid("invalid_callout", "callout digit must be in 0..9"));
  }
  if (!finite_point(shape.stem_x, shape.stem_y)) {
    return std::unexpected(
        make_invalid("invalid_callout", "callout stem tip must be finite"));
  }

  Rect2f badge{.x = shape.cx - shape.radius,
               .y = shape.cy - shape.radius,
               .width = shape.radius * 2.0f,
               .height = shape.radius * 2.0f};

  std::vector<DrawOp> ops;
  ops.push_back(make_fill_ellipse(badge, shape.fill));

  const bool has_stem = std::fabs(shape.stem_x - shape.cx) > 1.0e-3f ||
                        std::fabs(shape.stem_y - shape.cy) > 1.0e-3f;
  if (has_stem) {
    ops.push_back(make_line(shape.cx, shape.cy, shape.stem_x, shape.stem_y,
                            std::max(1.0f, shape.radius * 0.12f),
                            shape.stem_color));
  }

  // 7-segment digit inside the badge using stroke_line only.
  const float s = shape.radius * 0.55f;
  const float cx = shape.cx;
  const float cy = shape.cy;
  const float left = cx - s * 0.45f;
  const float right = cx + s * 0.45f;
  const float top = cy - s * 0.7f;
  const float mid = cy;
  const float bottom = cy + s * 0.7f;
  const float sw = std::max(1.0f, shape.radius * 0.12f);
  const std::uint8_t mask = seven_seg_mask(shape.digit);
  const struct Seg {
    std::uint8_t bit;
    float x0, y0, x1, y1;
  } segs[] = {
      {1 << 0, left, top, right, top},       // a
      {1 << 1, right, top, right, mid},      // b
      {1 << 2, right, mid, right, bottom},   // c
      {1 << 3, left, bottom, right, bottom}, // d
      {1 << 4, left, mid, left, bottom},     // e
      {1 << 5, left, top, left, mid},        // f
      {1 << 6, left, mid, right, mid},       // g
  };
  for (const auto &seg : segs) {
    if ((mask & seg.bit) != 0) {
      ops.push_back(make_line(seg.x0, seg.y0, seg.x1, seg.y1, sw, shape.ink));
    }
  }
  return ops;
}

[[nodiscard]] std::expected<std::vector<DrawOp>, diagnostics::Error>
lower_highlight(const HighlightShape &shape) {
  if (!is_valid_rect(shape.bounds) || !std::isfinite(shape.bounds.x) ||
      !std::isfinite(shape.bounds.y)) {
    return std::unexpected(make_invalid("invalid_highlight",
                                        "highlight bounds must be finite > 0"));
  }
  if (!std::isfinite(shape.corner_radius) || shape.corner_radius < 0.0f) {
    return std::unexpected(make_invalid(
        "invalid_highlight", "corner_radius must be finite and >= 0"));
  }
  std::vector<DrawOp> ops;
  if (shape.corner_radius > 0.0f) {
    ops.push_back(
        make_fill_rounded(shape.bounds, shape.corner_radius, shape.color));
  } else {
    ops.push_back(make_fill_rect(shape.bounds, shape.color));
  }
  return ops;
}

} // namespace

GraphicShapeKind graphic_shape_kind(const GraphicShape &shape) noexcept {
  return std::visit(
      [](const auto &value) -> GraphicShapeKind {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ArrowShape>) {
          return GraphicShapeKind::arrow;
        } else if constexpr (std::is_same_v<T, TechnicalLineShape>) {
          return GraphicShapeKind::technical_line;
        } else if constexpr (std::is_same_v<T, OutlineShape>) {
          return GraphicShapeKind::outline;
        } else if constexpr (std::is_same_v<T, StampShape>) {
          return GraphicShapeKind::stamp;
        } else if constexpr (std::is_same_v<T, NumericCalloutShape>) {
          return GraphicShapeKind::numeric_callout;
        } else {
          return GraphicShapeKind::highlight;
        }
      },
      shape);
}

std::expected<std::vector<DrawOp>, diagnostics::Error>
lower_graphic_shape(const GraphicShape &shape) {
  return std::visit(
      [](const auto &value)
          -> std::expected<std::vector<DrawOp>, diagnostics::Error> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ArrowShape>) {
          return lower_arrow(value);
        } else if constexpr (std::is_same_v<T, TechnicalLineShape>) {
          return lower_technical_line(value);
        } else if constexpr (std::is_same_v<T, OutlineShape>) {
          return lower_outline(value);
        } else if constexpr (std::is_same_v<T, StampShape>) {
          return lower_stamp(value);
        } else if constexpr (std::is_same_v<T, NumericCalloutShape>) {
          return lower_numeric_callout(value);
        } else {
          return lower_highlight(value);
        }
      },
      shape);
}

std::expected<PassDesc, diagnostics::Error>
compose_graphic_shape_pass(const GraphicShapeScene &scene) {
  if (scene.width_px == 0 || scene.height_px == 0) {
    return std::unexpected(make_invalid(
        "invalid_extent", "graphic shape scene width/height must be > 0"));
  }
  if (scene.shapes.empty()) {
    return std::unexpected(
        make_invalid("empty_scene", "graphic shape scene has no shapes"));
  }

  PassDesc pass;
  pass.width_px = scene.width_px;
  pass.height_px = scene.height_px;
  pass.color_target = 1;
  pass.ops.push_back(
      DrawOp{.kind = DrawOpKind::clear, .color = scene.clear_color});

  for (const auto &shape : scene.shapes) {
    auto lowered = lower_graphic_shape(shape);
    if (!lowered) {
      return std::unexpected(lowered.error());
    }
    // Every component must expand to shared primitives only.
    for (const auto &op : *lowered) {
      if (op.kind != DrawOpKind::fill_rect &&
          op.kind != DrawOpKind::fill_ellipse &&
          op.kind != DrawOpKind::fill_rounded_rect &&
          op.kind != DrawOpKind::stroke_line) {
        return std::unexpected(
            make_invalid("unsupported_primitive",
                         "graphic shape lowering escaped shared DrawOp kinds"));
      }
      pass.ops.push_back(op);
    }
  }

  if (auto valid = validate_pass(pass); !valid) {
    return std::unexpected(valid.error());
  }
  return pass;
}

std::expected<Rgba8Image, diagnostics::Error>
rasterize_graphic_shape_scene(const GraphicShapeScene &scene) {
  auto pass = compose_graphic_shape_pass(scene);
  if (!pass) {
    return std::unexpected(pass.error());
  }
  Rgba8Image image;
  if (auto ok = rasterize_pass(image, *pass); !ok) {
    return std::unexpected(ok.error());
  }
  return image;
}

} // namespace instantee::render_core
