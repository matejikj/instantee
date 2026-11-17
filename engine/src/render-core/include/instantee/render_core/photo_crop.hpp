#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/clip_compositor.hpp>
#include <instantee/render_core/draw_ops.hpp>

#include <cstdint>
#include <expected>
#include <string_view>

namespace instantee::render_core {

// Editorial photograph placement. Domain-neutral: produces crop geometry and a
// ClipScript over existing scissor/mask primitives (no Metal / style forks).
enum class PhotoFitMode : std::uint8_t {
  cover = 1,   // scale to cover the frame; clip rejects overflow
  contain = 2, // scale to fit inside the frame (letterbox)
};

enum class PhotoCutoutKind : std::uint8_t {
  none = 1,
  rounded_rect,
  ellipse,
};

struct PhotoCropRequest {
  float source_width_px = 0;
  float source_height_px = 0;
  Rect2f frame{};
  PhotoFitMode fit = PhotoFitMode::cover;
  PhotoCutoutKind cutout = PhotoCutoutKind::none;
  float cutout_corner_radius = 0;
  // Sharp edge frame thickness in px (0 disables). Drawn after the photo clip.
  float edge_frame_px = 0;
  Rgba8 photo_color{160, 160, 160, 255};
  Rgba8 edge_color{0, 0, 0, 255};
  Rgba8 background{255, 255, 255, 255};
  std::uint32_t canvas_width_px = 0;
  std::uint32_t canvas_height_px = 0;
};

struct PhotoCropPlan {
  Rect2f image_dest{};
  Rect2f clip_frame{};
  Rect2f visible_intersection{};
  float scale = 1.0f;
  PhotoFitMode fit = PhotoFitMode::cover;
  PhotoCutoutKind cutout = PhotoCutoutKind::none;
  // True only when visible photo coverage would escape the frame (must be
  // false for an accepted plan — clipping must not overflow).
  bool clipping_overflows_frame = false;
};

[[nodiscard]] constexpr std::string_view
photo_fit_mode_id(PhotoFitMode mode) noexcept {
  switch (mode) {
  case PhotoFitMode::cover:
    return "cover";
  case PhotoFitMode::contain:
    return "contain";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view
photo_cutout_kind_id(PhotoCutoutKind kind) noexcept {
  switch (kind) {
  case PhotoCutoutKind::none:
    return "none";
  case PhotoCutoutKind::rounded_rect:
    return "rounded_rect";
  case PhotoCutoutKind::ellipse:
    return "ellipse";
  }
  return "unknown";
}

// Compute cover/contain placement. Rejects non-finite sizes and empty frames.
[[nodiscard]] std::expected<PhotoCropPlan, diagnostics::Error>
plan_photo_crop(const PhotoCropRequest &request);

// Build: clear → scissor(frame) → optional cutout mask → photo fill → pops →
// optional sharp edge framing. Visible photo coverage stays inside the frame.
[[nodiscard]] std::expected<ClipScript, diagnostics::Error>
build_photo_crop_clip_script(const PhotoCropRequest &request,
                             const PhotoCropPlan &plan);

} // namespace instantee::render_core
