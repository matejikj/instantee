#include <instantee/direction/editorial_transitions.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "editorial_transitions", message};
}

[[nodiscard]] bool finite_positive(double v) noexcept {
  return std::isfinite(v) && v > 0.0;
}

[[nodiscard]] bool finite_unit_open(double v) noexcept {
  return std::isfinite(v) && v > 0.0 && v <= 1.0;
}

[[nodiscard]] bool valid_focus(const ContentSafeRect &r) noexcept {
  return std::isfinite(r.x_px) && std::isfinite(r.y_px) &&
         finite_positive(r.width_px) && finite_positive(r.height_px);
}

[[nodiscard]] double clamp01(double t) noexcept {
  return std::min(1.0, std::max(0.0, t));
}

[[nodiscard]] bool rect_inside(const ContentSafeRect &inner,
                               const ContentSafeRect &outer) noexcept {
  const double ix1 = inner.x_px + inner.width_px;
  const double iy1 = inner.y_px + inner.height_px;
  const double ox1 = outer.x_px + outer.width_px;
  const double oy1 = outer.y_px + outer.height_px;
  constexpr double kEps = 1.0e-9;
  return inner.x_px + kEps >= outer.x_px && inner.y_px + kEps >= outer.y_px &&
         ix1 <= ox1 + kEps && iy1 <= oy1 + kEps;
}

[[nodiscard]] ContentSafeRect scale_rect_about_center(const ContentSafeRect &r,
                                                      double scale) noexcept {
  const double cx = r.x_px + r.width_px * 0.5;
  const double cy = r.y_px + r.height_px * 0.5;
  const double w = r.width_px * scale;
  const double h = r.height_px * scale;
  return ContentSafeRect{.x_px = cx - w * 0.5,
                         .y_px = cy - h * 0.5,
                         .width_px = w,
                         .height_px = h};
}

[[nodiscard]] ContentSafeRect offset_rect(const ContentSafeRect &r, double dx,
                                          double dy) noexcept {
  return ContentSafeRect{.x_px = r.x_px + dx,
                         .y_px = r.y_px + dy,
                         .width_px = r.width_px,
                         .height_px = r.height_px};
}

[[nodiscard]] TransitionKind
kind_for_preset(EditorialTransitionPreset preset) noexcept {
  switch (preset) {
  case EditorialTransitionPreset::hard_cut:
    return TransitionKind::hard_cut;
  case EditorialTransitionPreset::crop_wipe:
    return TransitionKind::wipe;
  case EditorialTransitionPreset::rapid_zoom:
    // Zoom gesture rides a reveal/dissolve span so overlap stays explicit.
    return TransitionKind::reveal;
  case EditorialTransitionPreset::camera_snap:
    return TransitionKind::match_cut;
  }
  return TransitionKind::hard_cut;
}

} // namespace

std::expected<EditorialTransitionProgram, diagnostics::Error>
compile_editorial_transition(const EditorialTransitionRequest &request) {
  if (request.id.empty() || request.from_shot_id.empty() ||
      request.to_shot_id.empty()) {
    return std::unexpected(make_invalid(
        "invalid_ids", "editorial transition ids must be non-empty"));
  }
  if (!finite_unit_open(request.intensity)) {
    return std::unexpected(make_invalid("invalid_intensity",
                                        "intensity must be finite in (0, 1]"));
  }
  if (!finite_positive(request.canvas_width_px) ||
      !finite_positive(request.canvas_height_px)) {
    return std::unexpected(make_invalid(
        "invalid_canvas", "canvas extents must be finite and > 0"));
  }
  if (!valid_focus(request.type_focus)) {
    return std::unexpected(make_invalid(
        "invalid_type_focus", "type_focus must be a finite positive rect"));
  }

  // Safe-area policy must resolve for the canvas (rejects bad insets).
  auto safe = content_safe_rect(request.safe_area, request.canvas_width_px,
                                request.canvas_height_px);
  if (!safe) {
    return std::unexpected(safe.error());
  }
  if (!rect_inside(request.type_focus, *safe)) {
    return std::unexpected(
        make_invalid("type_focus_outside_safe_area",
                     "type_focus must start inside the content safe area"));
  }

  const bool is_cut = request.preset == EditorialTransitionPreset::hard_cut ||
                      request.preset == EditorialTransitionPreset::camera_snap;
  if (is_cut) {
    if (request.duration_frames != 0) {
      return std::unexpected(
          make_invalid("cut_requires_zero_duration",
                       "hard_cut/camera_snap require duration_frames == 0"));
    }
  } else if (request.duration_frames == 0) {
    return std::unexpected(
        make_invalid("gesture_requires_duration",
                     "crop_wipe/rapid_zoom require duration_frames > 0"));
  }

  EditorialTransitionProgram program;
  program.preset = request.preset;
  program.emphasis = request.emphasis;
  program.intensity = request.intensity;
  program.duration_frames = request.duration_frames;
  program.safe_area = request.safe_area;
  program.canvas_width_px = request.canvas_width_px;
  program.canvas_height_px = request.canvas_height_px;
  program.type_focus = request.type_focus;

  program.intent.id = request.id;
  program.intent.from_shot_id = request.from_shot_id;
  program.intent.to_shot_id = request.to_shot_id;
  program.intent.kind = kind_for_preset(request.preset);
  if (is_cut) {
    program.intent.overlap_frames = 0;
  } else {
    program.intent.overlap_frames = request.duration_frames;
  }
  return program;
}

std::expected<EditorialTransitionFrame, diagnostics::Error>
sample_editorial_transition(const EditorialTransitionProgram &program,
                            std::uint64_t local_frame) {
  const bool is_cut = program.duration_frames == 0;
  if (is_cut) {
    if (local_frame != 0) {
      return std::unexpected(make_invalid(
          "invalid_local_frame", "hard cut/snap only samples local_frame 0"));
    }
  } else if (local_frame >= program.duration_frames) {
    return std::unexpected(make_invalid(
        "invalid_local_frame", "local_frame must be in [0, duration_frames)"));
  }

  auto safe = content_safe_rect(program.safe_area, program.canvas_width_px,
                                program.canvas_height_px);
  if (!safe) {
    return std::unexpected(safe.error());
  }

  EditorialTransitionFrame frame;
  frame.local_frame = local_frame;
  if (is_cut) {
    frame.progress = 1.0;
  } else {
    frame.progress = clamp01(
        static_cast<double>(local_frame) /
        static_cast<double>(
            program.duration_frames > 1 ? program.duration_frames - 1 : 1));
  }

  switch (program.preset) {
  case EditorialTransitionPreset::hard_cut:
    frame.wipe_coverage = 1.0;
    frame.zoom_scale = 1.0;
    break;
  case EditorialTransitionPreset::crop_wipe:
    frame.wipe_coverage = frame.progress;
    frame.zoom_scale = 1.0;
    break;
  case EditorialTransitionPreset::rapid_zoom: {
    // Ease from punched-in to identity on the incoming side.
    const double punch = 1.0 + 0.35 * program.intensity;
    frame.zoom_scale = punch + (1.0 - punch) * frame.progress;
    frame.wipe_coverage = frame.progress;
    break;
  }
  case EditorialTransitionPreset::camera_snap: {
    // Single-frame snap impulse that settles immediately (duration 0 cut).
    frame.snap_offset_x_px = 6.0 * program.intensity;
    frame.snap_offset_y_px = -4.0 * program.intensity;
    frame.wipe_coverage = 1.0;
    frame.zoom_scale = 1.0;
    break;
  }
  }

  frame.type_scale = 1.0;
  frame.type_opacity = 1.0;
  switch (program.emphasis) {
  case TypographicEmphasis::none:
    break;
  case TypographicEmphasis::slam_in: {
    const double remain = 1.0 - frame.progress;
    frame.type_scale = 1.0 + 0.25 * program.intensity * remain * remain;
    break;
  }
  case TypographicEmphasis::mask_push:
    frame.type_opacity = clamp01(0.15 + 0.85 * frame.progress);
    break;
  }

  ContentSafeRect bounds = program.type_focus;
  bounds = scale_rect_about_center(bounds, frame.zoom_scale * frame.type_scale);
  bounds = offset_rect(bounds, frame.snap_offset_x_px, frame.snap_offset_y_px);
  frame.type_bounds = bounds;
  frame.type_inside_safe_area = rect_inside(bounds, *safe);

  // Camera snap offsets are clamped so type focus cannot leave the safe area.
  if (program.preset == EditorialTransitionPreset::camera_snap &&
      !frame.type_inside_safe_area) {
    const double max_dx = std::min(std::abs(frame.snap_offset_x_px),
                                   std::max(0.0, bounds.x_px - safe->x_px));
    const double max_dy =
        std::min(std::abs(frame.snap_offset_y_px),
                 std::max(0.0, safe->y_px + safe->height_px -
                                   (bounds.y_px + bounds.height_px)));
    frame.snap_offset_x_px =
        std::copysign(std::min(std::abs(frame.snap_offset_x_px), max_dx),
                      frame.snap_offset_x_px);
    frame.snap_offset_y_px =
        std::copysign(std::min(std::abs(frame.snap_offset_y_px), max_dy),
                      frame.snap_offset_y_px);
    bounds = offset_rect(
        scale_rect_about_center(program.type_focus,
                                frame.zoom_scale * frame.type_scale),
        frame.snap_offset_x_px, frame.snap_offset_y_px);
    frame.type_bounds = bounds;
    frame.type_inside_safe_area = rect_inside(bounds, *safe);
  }

  // Rapid zoom / slam must not enlarge type outside safe area — clamp scale.
  if (!frame.type_inside_safe_area &&
      (program.preset == EditorialTransitionPreset::rapid_zoom ||
       program.emphasis == TypographicEmphasis::slam_in)) {
    frame.zoom_scale = 1.0;
    frame.type_scale = 1.0;
    frame.snap_offset_x_px = 0.0;
    frame.snap_offset_y_px = 0.0;
    frame.type_bounds = program.type_focus;
    frame.type_inside_safe_area = rect_inside(frame.type_bounds, *safe);
  }

  return frame;
}

std::expected<EditorialTransitionVerification, diagnostics::Error>
verify_editorial_transition(std::span<const ShotTiming> shots,
                            const EditorialTransitionProgram &program) {
  std::uint64_t packed = 0;
  for (const auto &shot : shots) {
    if (shot.duration_frames >
        std::numeric_limits<std::uint64_t>::max() - packed) {
      return std::unexpected(make_invalid(
          "duration_overflow", "packed shot duration would overflow"));
    }
    packed += shot.duration_frames;
  }

  auto plan = plan_transitions(shots, std::span{&program.intent, 1});
  if (!plan) {
    return std::unexpected(plan.error());
  }

  EditorialTransitionVerification out;
  out.plan = std::move(*plan);
  out.packed_duration_frames = packed;
  out.duration_preserved = out.plan.film_duration_frames == packed;
  if (!out.duration_preserved) {
    return std::unexpected(
        make_invalid("duration_not_preserved",
                     "editorial transition must preserve packed frame count"));
  }

  const std::uint64_t samples =
      program.duration_frames == 0 ? 1 : program.duration_frames;
  out.frames.reserve(static_cast<std::size_t>(samples));
  out.safe_areas_ok = true;
  for (std::uint64_t f = 0; f < samples; ++f) {
    auto frame = sample_editorial_transition(program, f);
    if (!frame) {
      return std::unexpected(frame.error());
    }
    if (!frame->type_inside_safe_area) {
      out.safe_areas_ok = false;
    }
    out.frames.push_back(std::move(*frame));
  }
  if (!out.safe_areas_ok) {
    return std::unexpected(make_invalid(
        "safe_area_violation",
        "editorial transition pushed typography outside safe areas"));
  }
  return out;
}

} // namespace instantee::direction
