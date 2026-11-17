#include <instantee/render_core/clip_compositor.hpp>

#include <instantee/render_core/primitives.hpp>
#include <instantee/render_core/renderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <variant>
#include <vector>

namespace instantee::render_core {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "clip_compositor", message};
}

[[nodiscard]] bool is_finite_rect(const Rect2f &rect) noexcept {
  return std::isfinite(rect.x) && std::isfinite(rect.y) &&
         std::isfinite(rect.width) && std::isfinite(rect.height);
}

[[nodiscard]] bool is_mask_shape(DrawOpKind kind) noexcept {
  return kind == DrawOpKind::fill_rect || kind == DrawOpKind::fill_ellipse ||
         kind == DrawOpKind::fill_rounded_rect;
}

struct ScissorLayer {
  PixelScissor scissor{};
};

struct MaskLayer {
  DrawOp shape{};
};

using StackLayer = std::variant<ScissorLayer, MaskLayer>;

[[nodiscard]] float stack_visibility(const std::vector<StackLayer> &stack,
                                     float px, float py) noexcept {
  for (const auto &layer : stack) {
    if (const auto *scissor = std::get_if<ScissorLayer>(&layer)) {
      if (is_empty_scissor(scissor->scissor)) {
        return 0.0f;
      }
      const float x = px;
      const float y = py;
      if (x < static_cast<float>(scissor->scissor.x) ||
          y < static_cast<float>(scissor->scissor.y) ||
          x >=
              static_cast<float>(scissor->scissor.x + scissor->scissor.width) ||
          y >= static_cast<float>(scissor->scissor.y +
                                  scissor->scissor.height)) {
        return 0.0f;
      }
    } else if (const auto *mask = std::get_if<MaskLayer>(&layer)) {
      if (shape_coverage(mask->shape, px, py) <= 0.0f) {
        return 0.0f;
      }
    }
  }
  return 1.0f;
}

[[nodiscard]] PixelScissor
effective_scissor(const std::vector<StackLayer> &stack, std::uint32_t width_px,
                  std::uint32_t height_px) noexcept {
  PixelScissor current{.x = 0,
                       .y = 0,
                       .width = static_cast<std::int32_t>(width_px),
                       .height = static_cast<std::int32_t>(height_px)};
  for (const auto &layer : stack) {
    if (const auto *scissor = std::get_if<ScissorLayer>(&layer)) {
      current = intersect_scissors(current, scissor->scissor);
      if (is_empty_scissor(current)) {
        return current;
      }
    }
  }
  return current;
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

std::expected<void, diagnostics::Error>
draw_masked(Rgba8Image &image, const DrawOp &op,
            const std::vector<StackLayer> &stack) {
  if (op.kind == DrawOpKind::clear) {
    if (!stack.empty()) {
      return std::unexpected(make_invalid(
          "clear_with_clip_stack", "clear requires an empty clip/mask stack"));
    }
    return apply_draw_op(image, op);
  }

  // Bounds from the shape, further restricted by the effective scissor.
  PassDesc probe;
  probe.width_px = image.width_px;
  probe.height_px = image.height_px;
  probe.color_target = 1;
  probe.ops = {op};
  if (auto valid = validate_pass(probe); !valid) {
    return std::unexpected(valid.error());
  }

  const PixelScissor scissor =
      effective_scissor(stack, image.width_px, image.height_px);
  if (is_empty_scissor(scissor)) {
    return {};
  }

  for (std::int32_t y = scissor.y; y < scissor.y + scissor.height; ++y) {
    for (std::int32_t x = scissor.x; x < scissor.x + scissor.width; ++x) {
      const float px = static_cast<float>(x);
      const float py = static_cast<float>(y);
      const float vis = stack_visibility(stack, px, py);
      if (vis <= 0.0f) {
        continue;
      }
      const float cover = shape_coverage(op, px, py) * vis;
      if (cover <= 0.0f) {
        continue;
      }
      const std::size_t idx = (static_cast<std::size_t>(y) * image.width_px +
                               static_cast<std::size_t>(x)) *
                              4U;
      blend_pixel(image.rgba.data() + idx, op.color, cover);
    }
  }
  return {};
}

} // namespace

PixelScissor intersect_scissors(const PixelScissor &a,
                                const PixelScissor &b) noexcept {
  const std::int32_t x0 = std::max(a.x, b.x);
  const std::int32_t y0 = std::max(a.y, b.y);
  const std::int32_t x1 = std::min(a.x + a.width, b.x + b.width);
  const std::int32_t y1 = std::min(a.y + a.height, b.y + b.height);
  PixelScissor out{};
  out.x = x0;
  out.y = y0;
  out.width = std::max(0, x1 - x0);
  out.height = std::max(0, y1 - y0);
  return out;
}

PixelScissor scissor_from_rect(const Rect2f &rect, std::uint32_t width_px,
                               std::uint32_t height_px) noexcept {
  if (!(rect.width > 0.0f) || !(rect.height > 0.0f) || !is_finite_rect(rect) ||
      width_px == 0 || height_px == 0) {
    return {};
  }
  const std::int32_t x0 =
      std::max(0, static_cast<std::int32_t>(std::floor(rect.x)));
  const std::int32_t y0 =
      std::max(0, static_cast<std::int32_t>(std::floor(rect.y)));
  const std::int32_t x1 =
      std::min(static_cast<std::int32_t>(width_px),
               static_cast<std::int32_t>(std::ceil(rect.x + rect.width)));
  const std::int32_t y1 =
      std::min(static_cast<std::int32_t>(height_px),
               static_cast<std::int32_t>(std::ceil(rect.y + rect.height)));
  PixelScissor out{};
  out.x = x0;
  out.y = y0;
  out.width = std::max(0, x1 - x0);
  out.height = std::max(0, y1 - y0);
  return out;
}

std::expected<void, diagnostics::Error>
validate_clip_script(const ClipScript &script) {
  if (script.width_px == 0 || script.height_px == 0) {
    return std::unexpected(make_invalid(
        "invalid_clip_extent", "clip script width and height must be > 0"));
  }
  if (script.commands.empty()) {
    return std::unexpected(
        make_invalid("empty_clip_script", "clip script requires commands"));
  }

  std::uint32_t depth = 0;
  bool saw_draw = false;
  for (const auto &cmd : script.commands) {
    switch (cmd.kind) {
    case ClipCommandKind::push_scissor: {
      if (depth >= ClipScript::kMaxDepth) {
        return std::unexpected(make_invalid(
            "clip_depth_exceeded", "clip/mask nesting exceeds kMaxDepth"));
      }
      if (!is_finite_rect(cmd.scissor) || !is_valid_rect(cmd.scissor)) {
        return std::unexpected(make_invalid(
            "invalid_scissor", "push_scissor requires a finite positive AABB"));
      }
      ++depth;
      break;
    }
    case ClipCommandKind::push_shape_mask: {
      if (depth >= ClipScript::kMaxDepth) {
        return std::unexpected(make_invalid(
            "clip_depth_exceeded", "clip/mask nesting exceeds kMaxDepth"));
      }
      if (!is_mask_shape(cmd.op.kind)) {
        return std::unexpected(make_invalid(
            "invalid_mask_shape",
            "push_shape_mask requires fill_rect/ellipse/rounded_rect"));
      }
      PassDesc probe;
      probe.width_px = script.width_px;
      probe.height_px = script.height_px;
      probe.color_target = 1;
      probe.ops = {cmd.op};
      if (auto valid = validate_pass(probe); !valid) {
        return std::unexpected(valid.error());
      }
      ++depth;
      break;
    }
    case ClipCommandKind::pop:
      if (depth == 0) {
        return std::unexpected(
            make_invalid("clip_stack_underflow", "pop without matching push"));
      }
      --depth;
      break;
    case ClipCommandKind::draw: {
      PassDesc probe;
      probe.width_px = script.width_px;
      probe.height_px = script.height_px;
      probe.color_target = 1;
      probe.ops = {cmd.op};
      if (auto valid = validate_pass(probe); !valid) {
        return std::unexpected(valid.error());
      }
      if (cmd.op.kind == DrawOpKind::clear && depth != 0) {
        return std::unexpected(
            make_invalid("clear_with_clip_stack",
                         "clear requires an empty clip/mask stack"));
      }
      saw_draw = true;
      break;
    }
    default:
      return std::unexpected(
          make_invalid("unknown_clip_command", "unknown ClipCommandKind"));
    }
  }
  if (depth != 0) {
    return std::unexpected(make_invalid(
        "clip_stack_imbalance", "clip script ended with a non-empty stack"));
  }
  if (!saw_draw) {
    return std::unexpected(make_invalid("clip_script_no_draw",
                                        "clip script has no draw commands"));
  }
  return {};
}

std::expected<ClipStackStats, diagnostics::Error>
clip_script_stats(const ClipScript &script) {
  if (auto valid = validate_clip_script(script); !valid) {
    return std::unexpected(valid.error());
  }
  ClipStackStats stats{};
  std::uint32_t depth = 0;
  for (const auto &cmd : script.commands) {
    switch (cmd.kind) {
    case ClipCommandKind::push_scissor:
      ++depth;
      ++stats.scissor_pushes;
      stats.max_depth = std::max(stats.max_depth, depth);
      break;
    case ClipCommandKind::push_shape_mask:
      ++depth;
      ++stats.mask_pushes;
      stats.max_depth = std::max(stats.max_depth, depth);
      break;
    case ClipCommandKind::pop:
      --depth;
      ++stats.pops;
      break;
    case ClipCommandKind::draw:
      ++stats.draws;
      break;
    }
  }
  return stats;
}

std::expected<Rgba8Image, diagnostics::Error>
rasterize_clip_script(const ClipScript &script) {
  if (auto valid = validate_clip_script(script); !valid) {
    return std::unexpected(valid.error());
  }

  Rgba8Image image;
  image.width_px = script.width_px;
  image.height_px = script.height_px;
  image.rgba.assign(static_cast<std::size_t>(script.width_px) *
                        static_cast<std::size_t>(script.height_px) * 4U,
                    0);

  std::vector<StackLayer> stack;
  stack.reserve(ClipScript::kMaxDepth);

  for (const auto &cmd : script.commands) {
    switch (cmd.kind) {
    case ClipCommandKind::push_scissor:
      stack.push_back(
          ScissorLayer{.scissor = scissor_from_rect(
                           cmd.scissor, script.width_px, script.height_px)});
      break;
    case ClipCommandKind::push_shape_mask:
      stack.push_back(MaskLayer{.shape = cmd.op});
      break;
    case ClipCommandKind::pop:
      stack.pop_back();
      break;
    case ClipCommandKind::draw:
      if (auto drawn = draw_masked(image, cmd.op, stack); !drawn) {
        return std::unexpected(drawn.error());
      }
      break;
    }
  }
  return image;
}

} // namespace instantee::render_core
