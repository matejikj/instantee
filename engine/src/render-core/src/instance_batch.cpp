#include <instantee/render_core/instance_batch.hpp>

#include <instantee/render_core/primitives.hpp>
#include <instantee/render_core/renderer.hpp>

#include <string_view>

namespace instantee::render_core {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "instance_batch", message};
}

[[nodiscard]] bool is_shape(DrawOpKind kind) noexcept {
  switch (kind) {
  case DrawOpKind::fill_rect:
  case DrawOpKind::fill_ellipse:
  case DrawOpKind::fill_rounded_rect:
  case DrawOpKind::stroke_line:
    return true;
  case DrawOpKind::clear:
    return false;
  }
  return false;
}

[[nodiscard]] InstanceAttrib make_attrib(const DrawOp &op) noexcept {
  return InstanceAttrib{
      .rect = op.rect,
      .x1 = op.x1,
      .y1 = op.y1,
      .color = op.color,
      .corner_radius = op.corner_radius,
      .stroke_width = op.stroke_width,
      .kind = op.kind,
  };
}

[[nodiscard]] DrawOp make_op(const InstanceAttrib &inst,
                             std::uint64_t texture_binding) noexcept {
  return DrawOp{
      .kind = inst.kind,
      .rect = inst.rect,
      .x1 = inst.x1,
      .y1 = inst.y1,
      .color = inst.color,
      .corner_radius = inst.corner_radius,
      .stroke_width = inst.stroke_width,
      .texture_binding = texture_binding,
  };
}

} // namespace

std::expected<BatchedPass, diagnostics::Error>
batch_pass(const PassDesc &pass) {
  auto valid = validate_pass(pass);
  if (!valid) {
    return std::unexpected(valid.error());
  }

  BatchedPass out;
  out.width_px = pass.width_px;
  out.height_px = pass.height_px;
  out.color_target = pass.color_target;

  DrawBatch *current = nullptr;
  bool shapes_started = false;

  for (const DrawOp &op : pass.ops) {
    if (op.kind == DrawOpKind::clear) {
      if (shapes_started) {
        return std::unexpected(make_invalid(
            "mid_pass_clear_unsupported",
            "batch_pass requires clears before shape ops; split PassDesc"));
      }
      ++out.stats.clear_ops;
      out.clear_color = op.color;
      out.has_clear = true;
      continue;
    }
    if (!is_shape(op.kind)) {
      return std::unexpected(
          make_invalid("unsupported_draw_op", "unknown DrawOpKind in batcher"));
    }

    ++out.stats.source_ops;
    shapes_started = true;
    if (current == nullptr || current->texture_binding != op.texture_binding) {
      if (current != nullptr) {
        ++out.stats.batch_splits;
      }
      out.batches.push_back(
          DrawBatch{.texture_binding = op.texture_binding, .instances = {}});
      current = &out.batches.back();
    }
    current->instances.push_back(make_attrib(op));
  }

  out.stats.draw_calls = static_cast<std::uint32_t>(out.batches.size());
  return out;
}

PassDesc expand_batched_pass(const BatchedPass &batched) {
  PassDesc pass;
  pass.width_px = batched.width_px;
  pass.height_px = batched.height_px;
  pass.color_target = batched.color_target;
  if (batched.has_clear) {
    pass.ops.push_back(
        DrawOp{.kind = DrawOpKind::clear, .color = batched.clear_color});
  }
  for (const DrawBatch &batch : batched.batches) {
    for (const InstanceAttrib &inst : batch.instances) {
      pass.ops.push_back(make_op(inst, batch.texture_binding));
    }
  }
  return pass;
}

std::expected<void, diagnostics::Error>
rasterize_batched_pass(Rgba8Image &image, const BatchedPass &batched) {
  return rasterize_pass(image, expand_batched_pass(batched));
}

} // namespace instantee::render_core
