#include <instantee/render_core/visibility.hpp>

#include <instantee/render_core/renderer.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace instantee::render_core {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "visibility", message};
}

[[nodiscard]] bool is_shape(DrawOpKind kind) noexcept {
  return kind == DrawOpKind::fill_rect || kind == DrawOpKind::fill_ellipse ||
         kind == DrawOpKind::fill_rounded_rect ||
         kind == DrawOpKind::stroke_line;
}

[[nodiscard]] Rect2f viewport_rect(const Viewport2f &vp) noexcept {
  return Rect2f{vp.x, vp.y, vp.width, vp.height};
}

struct RankedItem {
  SceneDrawItem item{};
  std::size_t source_index = 0;
};

[[nodiscard]] bool draw_order_less(const RankedItem &a,
                                   const RankedItem &b) noexcept {
  if (a.item.layer_order != b.item.layer_order) {
    return a.item.layer_order < b.item.layer_order;
  }
  if (a.item.depth_z != b.item.depth_z) {
    return a.item.depth_z < b.item.depth_z;
  }
  if (a.item.stable_key != b.item.stable_key) {
    return a.item.stable_key < b.item.stable_key;
  }
  return a.source_index < b.source_index;
}

} // namespace

std::optional<Rect2f> draw_op_bounds(const DrawOp &op) noexcept {
  switch (op.kind) {
  case DrawOpKind::fill_rect:
  case DrawOpKind::fill_ellipse:
  case DrawOpKind::fill_rounded_rect:
    if (!is_valid_rect(op.rect) || !std::isfinite(op.rect.x) ||
        !std::isfinite(op.rect.y) || !std::isfinite(op.rect.width) ||
        !std::isfinite(op.rect.height)) {
      return std::nullopt;
    }
    return op.rect;
  case DrawOpKind::stroke_line: {
    if (!(op.stroke_width > 0.0f) || !std::isfinite(op.stroke_width) ||
        !std::isfinite(op.rect.x) || !std::isfinite(op.rect.y) ||
        !std::isfinite(op.x1) || !std::isfinite(op.y1)) {
      return std::nullopt;
    }
    const float half = op.stroke_width * 0.5f;
    const float min_x = std::min(op.rect.x, op.x1) - half;
    const float min_y = std::min(op.rect.y, op.y1) - half;
    const float max_x = std::max(op.rect.x, op.x1) + half;
    const float max_y = std::max(op.rect.y, op.y1) + half;
    return Rect2f{min_x, min_y, max_x - min_x, max_y - min_y};
  }
  case DrawOpKind::clear:
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<Rect2f> intersect_rects(const Rect2f &a,
                                      const Rect2f &b) noexcept {
  if (!is_valid_rect(a) || !is_valid_rect(b) || !std::isfinite(a.x) ||
      !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y)) {
    return std::nullopt;
  }
  const float x0 = std::max(a.x, b.x);
  const float y0 = std::max(a.y, b.y);
  const float x1 = std::min(a.x + a.width, b.x + b.width);
  const float y1 = std::min(a.y + a.height, b.y + b.height);
  if (!(x1 > x0) || !(y1 > y0)) {
    return std::nullopt;
  }
  return Rect2f{x0, y0, x1 - x0, y1 - y0};
}

bool rects_overlap(const Rect2f &a, const Rect2f &b) noexcept {
  return intersect_rects(a, b).has_value();
}

std::expected<CullSortResult, diagnostics::Error>
cull_and_sort_draws(std::span<const SceneDrawItem> items,
                    const Viewport2f &viewport) {
  if (!is_valid_viewport(viewport) || !std::isfinite(viewport.x) ||
      !std::isfinite(viewport.y) || !std::isfinite(viewport.width) ||
      !std::isfinite(viewport.height)) {
    return std::unexpected(make_invalid(
        "invalid_viewport", "viewport width/height must be finite and > 0"));
  }

  CullSortResult out;
  out.stats.candidates = static_cast<std::uint32_t>(items.size());
  std::vector<RankedItem> opaque;
  std::vector<RankedItem> transparent;
  opaque.reserve(items.size());
  transparent.reserve(items.size());

  const Rect2f vp = viewport_rect(viewport);

  for (std::size_t i = 0; i < items.size(); ++i) {
    const SceneDrawItem &item = items[i];
    if (item.op.kind == DrawOpKind::clear) {
      return std::unexpected(make_invalid(
          "invalid_draw_item",
          "clear ops are not SceneDrawItems; use build_culled_pass clear"));
    }
    if (!is_shape(item.op.kind)) {
      return std::unexpected(
          make_invalid("unsupported_draw_op", "unknown SceneDrawItem kind"));
    }
    if (!item.visible) {
      ++out.stats.culled_invisible;
      continue;
    }

    Rect2f effective_vp = vp;
    if (item.clip.has_value()) {
      auto clipped = intersect_rects(vp, *item.clip);
      if (!clipped) {
        ++out.stats.culled_empty_clip;
        continue;
      }
      effective_vp = *clipped;
    }

    const auto bounds = draw_op_bounds(item.op);
    if (!bounds || !rects_overlap(*bounds, effective_vp)) {
      ++out.stats.culled_offscreen;
      continue;
    }

    RankedItem ranked{.item = item, .source_index = i};
    if (item.transparent) {
      transparent.push_back(std::move(ranked));
    } else {
      opaque.push_back(std::move(ranked));
    }
  }

  std::stable_sort(opaque.begin(), opaque.end(), draw_order_less);
  std::stable_sort(transparent.begin(), transparent.end(), draw_order_less);

  out.draws.reserve(opaque.size() + transparent.size());
  for (const RankedItem &ranked : opaque) {
    out.draws.push_back(ranked.item.op);
  }
  for (const RankedItem &ranked : transparent) {
    out.draws.push_back(ranked.item.op);
  }
  out.stats.kept = static_cast<std::uint32_t>(out.draws.size());
  return out;
}

std::expected<PassDesc, diagnostics::Error>
build_culled_pass(std::span<const SceneDrawItem> items,
                  const Viewport2f &viewport, std::uint64_t color_target,
                  Rgba8 clear_color, CullSortStats *stats) {
  if (color_target == 0) {
    return std::unexpected(make_invalid("unbound_color_target",
                                        "culled pass requires a color_target"));
  }
  // Pass pixel space is origin-locked; a shifted viewport would desync draws.
  if (viewport.x != 0.0f || viewport.y != 0.0f) {
    return std::unexpected(
        make_invalid("invalid_viewport_origin",
                     "build_culled_pass requires viewport origin at (0, 0)"));
  }
  auto culled = cull_and_sort_draws(items, viewport);
  if (!culled) {
    return std::unexpected(culled.error());
  }
  if (stats != nullptr) {
    *stats = culled->stats;
  }

  PassDesc pass;
  pass.width_px = static_cast<std::uint32_t>(std::lround(viewport.width));
  pass.height_px = static_cast<std::uint32_t>(std::lround(viewport.height));
  // Integer pass extent must cover the float viewport used for culling.
  if (pass.width_px == 0 || pass.height_px == 0) {
    return std::unexpected(
        make_invalid("invalid_pass_extent",
                     "viewport does not map to a positive pass size"));
  }
  pass.color_target = color_target;
  pass.ops.reserve(culled->draws.size() + 1U);
  pass.ops.push_back(DrawOp{.kind = DrawOpKind::clear, .color = clear_color});
  pass.ops.insert(pass.ops.end(), culled->draws.begin(), culled->draws.end());

  if (auto valid = validate_pass(pass); !valid) {
    return std::unexpected(valid.error());
  }
  return pass;
}

std::expected<PassDesc, diagnostics::Error>
cull_offscreen_pass(const PassDesc &pass, CullSortStats *stats) {
  if (auto valid = validate_pass(pass); !valid) {
    return std::unexpected(valid.error());
  }

  CullSortStats local{};
  local.candidates = 0;
  PassDesc out;
  out.width_px = pass.width_px;
  out.height_px = pass.height_px;
  out.color_target = pass.color_target;
  out.ops.reserve(pass.ops.size());

  const Viewport2f viewport{0.0f, 0.0f, static_cast<float>(pass.width_px),
                            static_cast<float>(pass.height_px)};
  const Rect2f vp = viewport_rect(viewport);
  Rgba8 clear_color{0, 0, 0, 255};
  bool saw_shape = false;
  bool has_clear = false;

  for (const DrawOp &op : pass.ops) {
    if (op.kind == DrawOpKind::clear) {
      if (saw_shape) {
        return std::unexpected(make_invalid(
            "clear_after_shapes",
            "cull_offscreen_pass expects clears only before shapes"));
      }
      clear_color = op.color;
      has_clear = true;
      continue;
    }
    saw_shape = true;
    ++local.candidates;
    if (!is_shape(op.kind)) {
      return std::unexpected(
          make_invalid("unsupported_draw_op", "unknown DrawOpKind in pass"));
    }
    const auto bounds = draw_op_bounds(op);
    if (!bounds || !rects_overlap(*bounds, vp)) {
      ++local.culled_offscreen;
      continue;
    }
    out.ops.push_back(op);
    ++local.kept;
  }

  if (has_clear) {
    out.ops.insert(out.ops.begin(),
                   DrawOp{.kind = DrawOpKind::clear, .color = clear_color});
  }

  if (stats != nullptr) {
    *stats = local;
  }
  if (auto valid = validate_pass(out); !valid) {
    return std::unexpected(valid.error());
  }
  return out;
}

} // namespace instantee::render_core
