#include <instantee/render_core/photo_crop.hpp>

#include <instantee/render_core/visibility.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace instantee::render_core {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "photo_crop", message};
}

[[nodiscard]] bool finite_positive(float v) noexcept {
  return std::isfinite(v) && v > 0.0f;
}

[[nodiscard]] bool rect_contains(const Rect2f &outer,
                                 const Rect2f &inner) noexcept {
  if (!is_valid_rect(outer) || !is_valid_rect(inner)) {
    return false;
  }
  constexpr float kEps = 1e-3f;
  return inner.x + kEps >= outer.x && inner.y + kEps >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width + kEps &&
         inner.y + inner.height <= outer.y + outer.height + kEps;
}

} // namespace

std::expected<PhotoCropPlan, diagnostics::Error>
plan_photo_crop(const PhotoCropRequest &request) {
  if (!finite_positive(request.source_width_px) ||
      !finite_positive(request.source_height_px)) {
    return std::unexpected(
        make_invalid("invalid_source_size",
                     "Photograph source size must be finite and > 0"));
  }
  if (!is_valid_rect(request.frame) || !std::isfinite(request.frame.x) ||
      !std::isfinite(request.frame.y)) {
    return std::unexpected(
        make_invalid("invalid_frame", "Photograph frame must be a valid rect"));
  }
  if (request.canvas_width_px == 0 || request.canvas_height_px == 0) {
    return std::unexpected(make_invalid(
        "invalid_canvas", "Photograph crop canvas dimensions must be > 0"));
  }
  if (request.frame.x < 0.0f || request.frame.y < 0.0f ||
      request.frame.x + request.frame.width >
          static_cast<float>(request.canvas_width_px) + 1e-3f ||
      request.frame.y + request.frame.height >
          static_cast<float>(request.canvas_height_px) + 1e-3f) {
    return std::unexpected(make_invalid(
        "frame_outside_canvas", "Photograph frame must lie on the canvas"));
  }
  if (!std::isfinite(request.cutout_corner_radius) ||
      request.cutout_corner_radius < 0.0f ||
      !std::isfinite(request.edge_frame_px) || request.edge_frame_px < 0.0f) {
    return std::unexpected(make_invalid(
        "invalid_cutout_params",
        "Cutout radius and edge_frame_px must be finite and >= 0"));
  }
  if (request.edge_frame_px * 2.0f >= request.frame.width ||
      request.edge_frame_px * 2.0f >= request.frame.height) {
    return std::unexpected(make_invalid(
        "edge_frame_too_thick",
        "Edge frame thickness leaves no interior for the photograph"));
  }

  const float sx = request.frame.width / request.source_width_px;
  const float sy = request.frame.height / request.source_height_px;
  float scale = 1.0f;
  switch (request.fit) {
  case PhotoFitMode::cover:
    scale = std::max(sx, sy);
    break;
  case PhotoFitMode::contain:
    scale = std::min(sx, sy);
    break;
  }
  if (!std::isfinite(scale) || !(scale > 0.0f)) {
    return std::unexpected(make_invalid(
        "invalid_scale", "Photograph fit scale must be finite > 0"));
  }

  const float dest_w = request.source_width_px * scale;
  const float dest_h = request.source_height_px * scale;
  if (!std::isfinite(dest_w) || !std::isfinite(dest_h) ||
      dest_w > static_cast<float>(std::numeric_limits<std::int32_t>::max()) ||
      dest_h > static_cast<float>(std::numeric_limits<std::int32_t>::max())) {
    return std::unexpected(
        make_invalid("dest_overflow",
                     "Scaled photograph destination exceeds numeric limits"));
  }

  PhotoCropPlan plan;
  plan.fit = request.fit;
  plan.cutout = request.cutout;
  plan.scale = scale;
  plan.clip_frame = request.frame;
  plan.image_dest = Rect2f{
      request.frame.x + (request.frame.width - dest_w) * 0.5f,
      request.frame.y + (request.frame.height - dest_h) * 0.5f,
      dest_w,
      dest_h,
  };

  auto visible = intersect_rects(plan.image_dest, plan.clip_frame);
  if (!visible) {
    return std::unexpected(make_invalid(
        "empty_visible_crop", "Photograph does not intersect its frame"));
  }
  plan.visible_intersection = *visible;
  // After clipping to the frame, visible coverage must stay inside the frame.
  plan.clipping_overflows_frame =
      !rect_contains(plan.clip_frame, plan.visible_intersection);
  if (plan.clipping_overflows_frame) {
    return std::unexpected(make_invalid(
        "clip_overflow", "Photograph clipping would escape the frame"));
  }
  if (request.fit == PhotoFitMode::contain &&
      !rect_contains(plan.clip_frame, plan.image_dest)) {
    return std::unexpected(
        make_invalid("contain_overflow",
                     "Contain fit must keep the image inside the frame"));
  }
  return plan;
}

std::expected<ClipScript, diagnostics::Error>
build_photo_crop_clip_script(const PhotoCropRequest &request,
                             const PhotoCropPlan &plan) {
  if (plan.clipping_overflows_frame) {
    return std::unexpected(make_invalid(
        "clip_overflow", "Cannot build a clip script for an overflowing plan"));
  }
  if (plan.clip_frame.width != request.frame.width ||
      plan.clip_frame.height != request.frame.height) {
    return std::unexpected(make_invalid(
        "plan_request_mismatch", "Crop plan frame does not match the request"));
  }

  ClipScript script;
  script.width_px = request.canvas_width_px;
  script.height_px = request.canvas_height_px;
  script.commands.reserve(12);

  script.commands.push_back(ClipCommand{
      .kind = ClipCommandKind::draw,
      .op = {.kind = DrawOpKind::clear, .color = request.background}});

  // Frame scissor guarantees cover-fit draws cannot paint outside the frame.
  script.commands.push_back(ClipCommand{.kind = ClipCommandKind::push_scissor,
                                        .scissor = plan.clip_frame});

  if (request.cutout == PhotoCutoutKind::rounded_rect) {
    script.commands.push_back(
        ClipCommand{.kind = ClipCommandKind::push_shape_mask,
                    .op = {.kind = DrawOpKind::fill_rounded_rect,
                           .rect = plan.clip_frame,
                           .color = {255, 255, 255, 255},
                           .corner_radius = request.cutout_corner_radius}});
  } else if (request.cutout == PhotoCutoutKind::ellipse) {
    script.commands.push_back(
        ClipCommand{.kind = ClipCommandKind::push_shape_mask,
                    .op = {.kind = DrawOpKind::fill_ellipse,
                           .rect = plan.clip_frame,
                           .color = {255, 255, 255, 255}}});
  }

  script.commands.push_back(ClipCommand{.kind = ClipCommandKind::draw,
                                        .op = {.kind = DrawOpKind::fill_rect,
                                               .rect = plan.image_dest,
                                               .color = request.photo_color}});

  if (request.cutout != PhotoCutoutKind::none) {
    script.commands.push_back(ClipCommand{.kind = ClipCommandKind::pop});
  }
  script.commands.push_back(ClipCommand{.kind = ClipCommandKind::pop});

  // Sharp edge framing around the frame (outside the photo scissor stack).
  if (request.edge_frame_px > 0.0f) {
    const Rect2f &f = plan.clip_frame;
    const float t = request.edge_frame_px;
    const Rgba8 &c = request.edge_color;
    script.commands.push_back(ClipCommand{.kind = ClipCommandKind::draw,
                                          .op = {.kind = DrawOpKind::fill_rect,
                                                 .rect = {f.x, f.y, f.width, t},
                                                 .color = c}});
    script.commands.push_back(
        ClipCommand{.kind = ClipCommandKind::draw,
                    .op = {.kind = DrawOpKind::fill_rect,
                           .rect = {f.x, f.y + f.height - t, f.width, t},
                           .color = c}});
    script.commands.push_back(
        ClipCommand{.kind = ClipCommandKind::draw,
                    .op = {.kind = DrawOpKind::fill_rect,
                           .rect = {f.x, f.y + t, t, f.height - 2.0f * t},
                           .color = c}});
    script.commands.push_back(ClipCommand{
        .kind = ClipCommandKind::draw,
        .op = {.kind = DrawOpKind::fill_rect,
               .rect = {f.x + f.width - t, f.y + t, t, f.height - 2.0f * t},
               .color = c}});
  }

  if (auto ok = validate_clip_script(script); !ok) {
    return std::unexpected(ok.error());
  }
  return script;
}

} // namespace instantee::render_core
