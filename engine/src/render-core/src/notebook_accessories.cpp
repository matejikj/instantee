#include <instantee/render_core/notebook_accessories.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>

namespace instantee::render_core {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "notebook_accessories", message};
}

[[nodiscard]] bool unit01(float v) noexcept {
  return std::isfinite(v) && v >= 0.0f && v <= 1.0f;
}

[[nodiscard]] bool finite_rect(const Rect2f &r) noexcept {
  return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) &&
         std::isfinite(r.height) && r.width > 0.0f && r.height > 0.0f;
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

[[nodiscard]] DrawOp make_line(float x0, float y0, float x1, float y1,
                               float width, Rgba8 color) {
  return DrawOp{.kind = DrawOpKind::stroke_line,
                .rect = {.x = x0, .y = y0, .width = 0, .height = 0},
                .x1 = x1,
                .y1 = y1,
                .color = color,
                .stroke_width = width};
}

[[nodiscard]] LinearLayer layer_of(DrawOp shape, float opacity) {
  return LinearLayer{.shape = std::move(shape), .opacity = opacity};
}

[[nodiscard]] std::expected<std::vector<LinearLayer>, diagnostics::Error>
lower_tape(const TapeStripSpec &spec) {
  if (spec.id.empty() || !finite_rect(spec.bounds) ||
      !std::isfinite(spec.corner_radius) || spec.corner_radius < 0.0f ||
      !unit01(spec.opacity)) {
    return std::unexpected(
        make_invalid("accessory.bad_tape", "tape strip fields invalid"));
  }
  DrawOp shape =
      spec.corner_radius > 0.0f
          ? make_fill_rounded(spec.bounds, spec.corner_radius, spec.color)
          : make_fill_rect(spec.bounds, spec.color);
  return std::vector<LinearLayer>{layer_of(shape, spec.opacity)};
}

[[nodiscard]] std::expected<std::vector<LinearLayer>, diagnostics::Error>
lower_pin(const PinSpec &spec) {
  if (spec.id.empty() || !std::isfinite(spec.cx) || !std::isfinite(spec.cy) ||
      !std::isfinite(spec.head_radius) || !(spec.head_radius > 0.0f) ||
      !std::isfinite(spec.needle_length) || !(spec.needle_length > 0.0f) ||
      !unit01(spec.opacity)) {
    return std::unexpected(
        make_invalid("accessory.bad_pin", "pin fields invalid"));
  }
  const float d = spec.head_radius * 2.0f;
  Rect2f head{.x = spec.cx - spec.head_radius,
              .y = spec.cy - spec.head_radius,
              .width = d,
              .height = d};
  std::vector<LinearLayer> layers;
  layers.push_back(
      layer_of(make_fill_ellipse(head, spec.head_color), spec.opacity));
  layers.push_back(
      layer_of(make_line(spec.cx, spec.cy, spec.cx,
                         spec.cy + spec.needle_length, 1.5f, spec.needle_color),
               spec.opacity));
  return layers;
}

[[nodiscard]] std::expected<std::vector<LinearLayer>, diagnostics::Error>
lower_underline(const UnderlineSpec &spec) {
  if (spec.id.empty() || !std::isfinite(spec.x0) || !std::isfinite(spec.y0) ||
      !std::isfinite(spec.x1) || !std::isfinite(spec.y1) ||
      !std::isfinite(spec.stroke_width) || !(spec.stroke_width > 0.0f) ||
      !unit01(spec.opacity)) {
    return std::unexpected(
        make_invalid("accessory.bad_underline", "underline fields invalid"));
  }
  const float dx = spec.x1 - spec.x0;
  const float dy = spec.y1 - spec.y0;
  if (!(dx * dx + dy * dy > 1.0e-6f)) {
    return std::unexpected(make_invalid("accessory.bad_underline",
                                        "underline length must be > 0"));
  }
  return std::vector<LinearLayer>{
      layer_of(make_line(spec.x0, spec.y0, spec.x1, spec.y1, spec.stroke_width,
                         spec.color),
               spec.opacity)};
}

[[nodiscard]] std::expected<std::vector<LinearLayer>, diagnostics::Error>
lower_highlight(const InkHighlightSpec &spec) {
  if (spec.id.empty() || !finite_rect(spec.bounds) ||
      !std::isfinite(spec.corner_radius) || spec.corner_radius < 0.0f ||
      !unit01(spec.opacity)) {
    return std::unexpected(make_invalid("accessory.bad_highlight",
                                        "ink highlight fields invalid"));
  }
  DrawOp shape =
      spec.corner_radius > 0.0f
          ? make_fill_rounded(spec.bounds, spec.corner_radius, spec.color)
          : make_fill_rect(spec.bounds, spec.color);
  return std::vector<LinearLayer>{layer_of(shape, spec.opacity)};
}

[[nodiscard]] float
polyline_length(const std::vector<AccessoryPoint> &points) noexcept {
  float total = 0;
  for (std::size_t i = 1; i < points.size(); ++i) {
    const float dx = points[i].x - points[i - 1].x;
    const float dy = points[i].y - points[i - 1].y;
    total += std::sqrt(dx * dx + dy * dy);
  }
  return total;
}

[[nodiscard]] std::expected<std::vector<LinearLayer>, diagnostics::Error>
lower_marker(const MarkerStrokeSpec &spec) {
  if (spec.id.empty() || spec.points.size() < 2 || !unit01(spec.progress) ||
      !unit01(spec.opacity)) {
    return std::unexpected(
        make_invalid("accessory.bad_marker", "marker stroke fields invalid"));
  }
  for (const auto &p : spec.points) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
      return std::unexpected(
          make_invalid("accessory.bad_marker", "marker points must be finite"));
    }
  }
  const MarkerPresetStyle style = marker_draw_preset_style(spec.preset);
  const Rgba8 color = spec.color.a == 0 ? style.color : spec.color;
  const float total = polyline_length(spec.points);
  if (!(total > 1.0e-6f)) {
    return std::unexpected(
        make_invalid("accessory.bad_marker", "marker length must be > 0"));
  }
  const float budget = total * spec.progress;
  std::vector<LinearLayer> layers;
  float walked = 0;
  for (std::size_t i = 1; i < spec.points.size(); ++i) {
    if (walked >= budget) {
      break;
    }
    const auto &a = spec.points[i - 1];
    const auto &b = spec.points[i];
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float seg = std::sqrt(dx * dx + dy * dy);
    if (!(seg > 1.0e-6f)) {
      continue;
    }
    const float remain = budget - walked;
    const float t = std::min(1.0f, remain / seg);
    const float x1 = a.x + dx * t;
    const float y1 = a.y + dy * t;
    layers.push_back(layer_of(
        make_line(a.x, a.y, x1, y1, style.stroke_width, color), spec.opacity));
    walked += seg * t;
  }
  return layers;
}

} // namespace

MarkerPresetStyle marker_draw_preset_style(MarkerDrawPreset preset) noexcept {
  switch (preset) {
  case MarkerDrawPreset::highlighter_pen:
    return MarkerPresetStyle{.stroke_width = 8.0f,
                             .color = Rgba8{255, 230, 60, 96},
                             .uses_ink_stroke_feature = false};
  case MarkerDrawPreset::felt_tip:
    return MarkerPresetStyle{.stroke_width = 3.0f,
                             .color = Rgba8{20, 40, 90, 230},
                             .uses_ink_stroke_feature = true};
  case MarkerDrawPreset::pencil_thin:
    return MarkerPresetStyle{.stroke_width = 1.25f,
                             .color = Rgba8{60, 60, 70, 200},
                             .uses_ink_stroke_feature = true};
  }
  return {};
}

ShaderFeatureRegistry make_notebook_accessory_shader_feature_registry() {
  // Reuse ink_stroke registration so marker presets share the animated-ink
  // feature id instead of inventing a parallel marker shader.
  return make_ink_stroke_shader_feature_registry();
}

std::expected<std::vector<LinearLayer>, diagnostics::Error>
lower_notebook_accessory(const NotebookAccessory &accessory) {
  return std::visit(
      [](const auto &spec)
          -> std::expected<std::vector<LinearLayer>, diagnostics::Error> {
        using T = std::decay_t<decltype(spec)>;
        if constexpr (std::is_same_v<T, TapeStripSpec>) {
          return lower_tape(spec);
        } else if constexpr (std::is_same_v<T, PinSpec>) {
          return lower_pin(spec);
        } else if constexpr (std::is_same_v<T, UnderlineSpec>) {
          return lower_underline(spec);
        } else if constexpr (std::is_same_v<T, InkHighlightSpec>) {
          return lower_highlight(spec);
        } else {
          return lower_marker(spec);
        }
      },
      accessory);
}

std::expected<LinearCompositeScene, diagnostics::Error>
compose_notebook_accessory_scene(const NotebookAccessoryScene &scene) {
  if (scene.width_px == 0 || scene.height_px == 0) {
    return std::unexpected(
        make_invalid("accessory.bad_scene", "scene size must be > 0"));
  }
  LinearCompositeScene out;
  out.width_px = scene.width_px;
  out.height_px = scene.height_px;
  out.clear_color = scene.clear_color;

  for (const auto &op : scene.base_ops) {
    if (op.kind != DrawOpKind::fill_rect &&
        op.kind != DrawOpKind::fill_ellipse &&
        op.kind != DrawOpKind::fill_rounded_rect &&
        op.kind != DrawOpKind::stroke_line) {
      return std::unexpected(make_invalid(
          "accessory.bad_base", "base ops must be shared shape DrawOps"));
    }
    out.layers.push_back(LinearLayer{.shape = op, .opacity = 1.0f});
  }

  for (const auto &acc : scene.accessories) {
    auto layers = lower_notebook_accessory(acc);
    if (!layers) {
      return std::unexpected(layers.error());
    }
    for (auto &layer : *layers) {
      out.layers.push_back(std::move(layer));
    }
  }
  if (auto valid = validate_linear_composite_scene(out); !valid) {
    return std::unexpected(valid.error());
  }
  return out;
}

std::expected<Rgba8Image, diagnostics::Error>
rasterize_notebook_accessory_scene(const NotebookAccessoryScene &scene) {
  auto linear_scene = compose_notebook_accessory_scene(scene);
  if (!linear_scene) {
    return std::unexpected(linear_scene.error());
  }
  auto linear = rasterize_linear_composite(*linear_scene);
  if (!linear) {
    return std::unexpected(linear.error());
  }
  return encode_rec709_rgba8(*linear);
}

std::string format_notebook_accessory_golden(
    std::string_view label, const NotebookAccessoryScene &scene,
    const LinearCompositeScene &linear, const Rgba8Image &encoded) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3);
  oss << "notebook-accessories " << label << '\n';
  oss << "size " << scene.width_px << "x" << scene.height_px
      << " accessories=" << scene.accessories.size()
      << " layers=" << linear.layers.size() << '\n';
  oss << "import=srgb_straight working=linear_premul encode=rec709_srgb\n";
  for (const auto &acc : scene.accessories) {
    oss << "kind=" << notebook_accessory_kind_id(acc) << '\n';
    std::visit(
        [&](const auto &spec) {
          using T = std::decay_t<decltype(spec)>;
          oss << "  id=" << spec.id << " opacity=" << spec.opacity;
          if constexpr (std::is_same_v<T, MarkerStrokeSpec>) {
            oss << " preset=" << marker_draw_preset_id(spec.preset)
                << " progress=" << spec.progress << " ink_feature="
                << (marker_draw_preset_style(spec.preset)
                            .uses_ink_stroke_feature
                        ? 1
                        : 0);
          }
          oss << '\n';
        },
        acc);
  }
  oss << "layers\n";
  for (const auto &layer : linear.layers) {
    oss << "  kind=" << static_cast<int>(layer.shape.kind)
        << " opacity=" << layer.opacity
        << " a=" << static_cast<int>(layer.shape.color.a);
    if (layer.shape.kind == DrawOpKind::stroke_line) {
      oss << " line=" << layer.shape.rect.x << "," << layer.shape.rect.y << "->"
          << layer.shape.x1 << "," << layer.shape.y1
          << " w=" << layer.shape.stroke_width;
    } else {
      oss << " rect=" << layer.shape.rect.x << "," << layer.shape.rect.y << ","
          << layer.shape.rect.width << "x" << layer.shape.rect.height;
    }
    oss << '\n';
  }
  const std::uint32_t samples[][2] = {{4, 4},   {20, 12}, {40, 20},
                                      {60, 28}, {30, 40}, {50, 50}};
  for (const auto &s : samples) {
    if (s[0] >= encoded.width_px || s[1] >= encoded.height_px) {
      continue;
    }
    const std::size_t idx =
        (static_cast<std::size_t>(s[1]) * encoded.width_px + s[0]) * 4U;
    oss << "p" << s[0] << "," << s[1] << "="
        << static_cast<int>(encoded.rgba[idx]) << ","
        << static_cast<int>(encoded.rgba[idx + 1]) << ","
        << static_cast<int>(encoded.rgba[idx + 2]) << ","
        << static_cast<int>(encoded.rgba[idx + 3]) << '\n';
  }
  return oss.str();
}

} // namespace instantee::render_core
