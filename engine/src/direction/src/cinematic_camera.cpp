#include <instantee/direction/cinematic_camera.hpp>

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_error(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "cinematic_camera", message};
}

struct ActionWindow {
  std::uint64_t start = 0;
  std::uint64_t end = 0;
};

[[nodiscard]] ActionWindow action_window(const ShotPlan &shot) noexcept {
  for (const ShotSubevent &ev : shot.subevents) {
    if (ev.kind == ShotSubeventKind::action && ev.duration_frames > 0) {
      return ActionWindow{.start = ev.start_frame,
                          .end = ev.start_frame + ev.duration_frames - 1};
    }
  }
  if (shot.duration_frames == 0) {
    return {};
  }
  return ActionWindow{.start = 0, .end = shot.duration_frames - 1};
}

[[nodiscard]] ActionWindow scale_window(ActionWindow window,
                                        double duration_scale) noexcept {
  if (!(duration_scale > 0.0) || !std::isfinite(duration_scale) ||
      window.end < window.start) {
    return window;
  }
  const double span = static_cast<double>(window.end - window.start) *
                      std::min(duration_scale, 1.0);
  const auto frames =
      static_cast<std::uint64_t>(std::llround(std::max(0.0, span)));
  return ActionWindow{.start = window.start, .end = window.start + frames};
}

struct MoveSpec {
  CameraCapability capability = CameraCapability::dolly;
  bool required = true;
  std::string_view property;
  double from = 0.0;
  double to = 0.0;
};

[[nodiscard]] std::vector<MoveSpec>
moves_for_preset(CinematicCameraPreset preset, double intensity) {
  const double peak = std::clamp(intensity, 0.0, 1.0);
  std::vector<MoveSpec> moves;
  switch (preset) {
  case CinematicCameraPreset::dolly:
    // Camera forward = -Z: positive intensity pushes in toward the subject.
    // Amplitudes stay small vs typical stage depths so framing remains stable.
    moves.push_back({.capability = CameraCapability::dolly,
                     .required = true,
                     .property = "translate_z",
                     .from = 0.0,
                     .to = -2.5 * peak});
    break;
  case CinematicCameraPreset::truck:
    moves.push_back({.capability = CameraCapability::pan,
                     .required = true,
                     .property = "translate_x",
                     .from = 0.0,
                     .to = 1.8 * peak});
    break;
  case CinematicCameraPreset::slow_zoom:
    moves.push_back({.capability = CameraCapability::zoom,
                     .required = true,
                     .property = "scale_x",
                     .from = 1.0,
                     .to = 1.0 + 0.06 * peak});
    moves.push_back({.capability = CameraCapability::zoom,
                     .required = true,
                     .property = "scale_y",
                     .from = 1.0,
                     .to = 1.0 + 0.06 * peak});
    break;
  case CinematicCameraPreset::crane_pan:
    moves.push_back({.capability = CameraCapability::pan,
                     .required = true,
                     .property = "translate_y",
                     .from = 0.0,
                     .to = -1.2 * peak}); // rise (canvas +Y down → negative)
    moves.push_back({.capability = CameraCapability::pan,
                     .required = true,
                     .property = "translate_x",
                     .from = 0.0,
                     .to = 1.4 * peak});
    break;
  case CinematicCameraPreset::rack_depth:
    // Focus-pull approximation: small axial shift under the focus capability.
    // True optical DOF remains out of scope; parallax focus is sampled in
    // demos.
    moves.push_back({.capability = CameraCapability::focus,
                     .required = true,
                     .property = "translate_z",
                     .from = 0.0,
                     .to = -1.2 * peak});
    moves.push_back({.capability = CameraCapability::dolly,
                     .required = false,
                     .property = "translate_z",
                     .from = 0.0,
                     .to = -0.6 * peak});
    break;
  }
  return moves;
}

[[nodiscard]] std::expected<std::vector<MoveSpec>, diagnostics::Error>
select_allowed_moves(const CameraCapabilitySet &allowed,
                     std::span<const MoveSpec> candidates) {
  std::vector<MoveSpec> selected;
  selected.reserve(candidates.size());
  for (const MoveSpec &move : candidates) {
    const bool permitted = allowed.contains(move.capability);
    if (!permitted) {
      if (move.required) {
        return std::unexpected(make_error(
            "undeclared_camera_capability",
            "Cinematic preset requires a camera capability not declared by the "
            "director"));
      }
      continue;
    }
    selected.push_back(move);
  }
  return selected;
}

[[nodiscard]] double clamp01(double t) noexcept {
  if (!std::isfinite(t)) {
    return 0.0;
  }
  return std::clamp(t, 0.0, 1.0);
}

} // namespace

double cinematic_ease(MotionEase ease, double t) noexcept {
  t = clamp01(t);
  switch (ease) {
  case MotionEase::linear:
    return t;
  case MotionEase::ease_in:
    return t * t;
  case MotionEase::ease_out: {
    const double u = 1.0 - t;
    return 1.0 - u * u;
  }
  case MotionEase::ease_in_out:
    return t * t * (3.0 - 2.0 * t);
  case MotionEase::spring: {
    // Smooth, non-oscillatory stand-in so presets never inject jitter.
    return t * t * (3.0 - 2.0 * t);
  }
  }
  return t;
}

std::vector<CameraCapability>
required_capabilities_for_preset(CinematicCameraPreset preset) {
  std::vector<CameraCapability> required;
  for (const MoveSpec &move : moves_for_preset(preset, 1.0)) {
    if (!move.required) {
      continue;
    }
    if (std::find(required.begin(), required.end(), move.capability) ==
        required.end()) {
      required.push_back(move.capability);
    }
  }
  return required;
}

std::expected<CinematicCameraPreset, diagnostics::Error>
parse_cinematic_camera_preset(std::string_view text) {
  if (text == "dolly") {
    return CinematicCameraPreset::dolly;
  }
  if (text == "truck") {
    return CinematicCameraPreset::truck;
  }
  if (text == "slow_zoom") {
    return CinematicCameraPreset::slow_zoom;
  }
  if (text == "crane_pan") {
    return CinematicCameraPreset::crane_pan;
  }
  if (text == "rack_depth") {
    return CinematicCameraPreset::rack_depth;
  }
  return std::unexpected(make_error("unknown_cinematic_preset",
                                    "Unknown cinematic camera preset"));
}

std::expected<std::vector<DirectedTrack>, diagnostics::Error>
plan_cinematic_camera_preset(const CinematicCameraRequest &request) {
  if (request.camera_node_id.empty()) {
    return std::unexpected(
        make_error("invalid_camera_node", "Camera node id must be non-empty"));
  }
  if (request.shot.duration_frames == 0 || request.shot.shot_id.empty()) {
    return std::unexpected(make_error(
        "invalid_shot_plan", "Cinematic presets require a planned shot"));
  }
  if (!std::isfinite(request.intensity) || request.intensity < 0.0 ||
      request.intensity > 1.0) {
    return std::unexpected(
        make_error("invalid_intensity", "Intensity must be finite in [0, 1]"));
  }

  double intensity = request.intensity;
  ActionWindow window = action_window(request.shot);
  if (request.grammar) {
    if (request.grammar->pattern != GesturePattern::camera_move) {
      return std::unexpected(make_error(
          "invalid_grammar_pattern",
          "Cinematic presets accept only camera_move Motion Grammar bindings"));
    }
    if (!(request.grammar->duration_scale > 0.0) ||
        !std::isfinite(request.grammar->duration_scale) ||
        request.grammar->duration_scale > 1.0) {
      return std::unexpected(
          make_error("invalid_grammar_duration",
                     "Motion Grammar duration_scale must be finite in (0, 1]"));
    }
    if (!std::isfinite(request.grammar->intensity_scale) ||
        request.grammar->intensity_scale < 0.0) {
      return std::unexpected(
          make_error("invalid_grammar_intensity",
                     "Motion Grammar intensity_scale must be finite and >= 0"));
    }
    intensity =
        std::clamp(intensity * request.grammar->intensity_scale, 0.0, 1.0);
    window = scale_window(window, request.grammar->duration_scale);
  }

  if (window.end < window.start || window.end >= request.shot.duration_frames) {
    return std::unexpected(make_error(
        "invalid_action_window",
        "Preset action window is outside the author duration budget"));
  }

  const auto candidates = moves_for_preset(request.preset, intensity);
  auto selected = select_allowed_moves(request.allowed, candidates);
  if (!selected) {
    return std::unexpected(selected.error());
  }
  if (selected->empty()) {
    return std::vector<DirectedTrack>{};
  }

  // Combine duplicate properties (e.g. rack optional dolly + focus both on z)
  // into a single track so sampling stays jitter-free.
  std::vector<DirectedTrack> tracks;
  tracks.reserve(selected->size());
  for (std::size_t i = 0; i < selected->size(); ++i) {
    const MoveSpec &spec = (*selected)[i];
    DirectedTrack *existing = nullptr;
    for (DirectedTrack &track : tracks) {
      if (track.property == spec.property &&
          track.target_id == request.camera_node_id) {
        existing = &track;
        break;
      }
    }
    if (existing) {
      // Sum destination deltas relative to a shared origin of 0 or 1 for scale.
      if (existing->keyframes.size() >= 2) {
        existing->keyframes.back().value += spec.to - spec.from;
      }
      continue;
    }

    DirectedTrack track;
    track.id = request.shot.shot_id + "_cine_" +
               std::string(cinematic_camera_preset_id(request.preset)) + "_" +
               std::string(camera_capability_id(spec.capability)) + "_" +
               std::string(spec.property) + "_" + std::to_string(i);
    track.target_id = request.camera_node_id;
    track.property = std::string(spec.property);
    track.keyframes.push_back(
        DirectedKeyframe{.frame = window.start, .value = spec.from});
    if (window.end != window.start) {
      track.keyframes.push_back(
          DirectedKeyframe{.frame = window.end, .value = spec.to});
    } else {
      track.keyframes.front().value = spec.to;
    }
    tracks.push_back(std::move(track));
  }
  return tracks;
}

std::expected<double, diagnostics::Error>
sample_directed_track_value(const DirectedTrack &track, std::uint64_t frame,
                            MotionEase ease) {
  if (track.keyframes.empty()) {
    return std::unexpected(
        make_error("empty_track", "Cannot sample a track with no keyframes"));
  }
  if (track.keyframes.size() == 1) {
    return track.keyframes.front().value;
  }
  // Assume sorted ascending frames from the planner (two keys).
  const DirectedKeyframe &a = track.keyframes.front();
  const DirectedKeyframe &b = track.keyframes.back();
  if (b.frame < a.frame) {
    return std::unexpected(
        make_error("unsorted_keyframes", "Track keyframes must be ascending"));
  }
  if (frame <= a.frame) {
    return a.value;
  }
  if (frame >= b.frame) {
    return b.value;
  }
  const double span = static_cast<double>(b.frame - a.frame);
  const double t =
      cinematic_ease(ease, static_cast<double>(frame - a.frame) / span);
  return a.value + (b.value - a.value) * t;
}

std::expected<core::LocalTransform, diagnostics::Error>
sample_camera_local_from_tracks(const std::vector<DirectedTrack> &tracks,
                                std::uint64_t frame, MotionEase ease) {
  core::LocalTransform local{};
  local.scale = {1, 1, 1};
  for (const DirectedTrack &track : tracks) {
    auto value = sample_directed_track_value(track, frame, ease);
    if (!value) {
      return std::unexpected(value.error());
    }
    if (track.property == "translate_x") {
      local.translate.x = *value;
    } else if (track.property == "translate_y") {
      local.translate.y = *value;
    } else if (track.property == "translate_z") {
      local.translate.z = *value;
    } else if (track.property == "scale_x") {
      local.scale.x = *value;
    } else if (track.property == "scale_y") {
      local.scale.y = *value;
    } else if (track.property == "scale_z") {
      local.scale.z = *value;
    } else if (track.property == "rotation_z_rad") {
      local.rotation_z_rad = *value;
    } else {
      return std::unexpected(make_error(
          "unsupported_camera_property",
          "Cinematic sampler only applies TRS camera node properties"));
    }
  }
  return local;
}

std::expected<std::vector<CinematicSample>, diagnostics::Error>
sample_cinematic_preset_on_stage(const CinematicCameraRequest &request,
                                 const core::DepthStage &stage,
                                 std::size_t subject_plane_index,
                                 const core::PerspectiveFrustum &frustum,
                                 const core::Viewport &viewport) {
  if (auto ok = core::validate_depth_stage(stage); !ok) {
    return std::unexpected(ok.error());
  }
  if (subject_plane_index >= stage.planes.size()) {
    return std::unexpected(make_error(
        "invalid_subject_plane", "Subject plane index is out of stage range"));
  }

  auto tracks = plan_cinematic_camera_preset(request);
  if (!tracks) {
    return std::unexpected(tracks.error());
  }
  const MotionEase ease =
      request.grammar ? request.grammar->ease : MotionEase::ease_in_out;

  std::vector<CinematicSample> samples;
  samples.reserve(static_cast<std::size_t>(request.shot.duration_frames));
  const core::PaperPlane &subject = stage.planes[subject_plane_index];
  for (std::uint64_t frame = 0; frame < request.shot.duration_frames; ++frame) {
    auto local = sample_camera_local_from_tracks(*tracks, frame, ease);
    if (!local) {
      return std::unexpected(local.error());
    }
    auto camera = core::make_perspective_camera(
        core::local_matrix(*local), frustum, viewport, core::AspectFit::stretch,
        viewport.width, viewport.height);
    if (!camera) {
      return std::unexpected(camera.error());
    }
    const core::Vec3 world = core::plane_anchor_world(stage, subject);
    auto pixel = core::project_world_to_pixel(*camera, world);
    if (!pixel) {
      return std::unexpected(pixel.error());
    }
    samples.push_back(CinematicSample{
        .frame = frame, .camera_local = *local, .anchor_pixel = *pixel});
  }
  return samples;
}

FramingStabilityReport
measure_framing_stability(const std::vector<CinematicSample> &samples,
                          double min_step_px, double max_jerk_px) {
  FramingStabilityReport report;
  if (samples.size() < 3) {
    report.ok = false;
    return report;
  }
  double prev_dx = 0.0;
  double prev_dy = 0.0;
  bool have_prev = false;
  int prev_sx = 0;
  int prev_sy = 0;
  for (std::size_t i = 1; i < samples.size(); ++i) {
    const double dx = samples[i].anchor_pixel.x - samples[i - 1].anchor_pixel.x;
    const double dy = samples[i].anchor_pixel.y - samples[i - 1].anchor_pixel.y;
    report.max_step_px = std::max(report.max_step_px, std::hypot(dx, dy));
    if (have_prev) {
      const double jerk = std::hypot(dx - prev_dx, dy - prev_dy);
      report.max_jerk_px = std::max(report.max_jerk_px, jerk);
      const int sx = (std::fabs(dx) >= min_step_px) ? (dx > 0.0 ? 1 : -1) : 0;
      const int sy = (std::fabs(dy) >= min_step_px) ? (dy > 0.0 ? 1 : -1) : 0;
      if (sx != 0 && prev_sx != 0 && sx != prev_sx) {
        ++report.direction_reversals;
      }
      if (sy != 0 && prev_sy != 0 && sy != prev_sy) {
        ++report.direction_reversals;
      }
      if (sx != 0) {
        prev_sx = sx;
      }
      if (sy != 0) {
        prev_sy = sy;
      }
    } else {
      prev_sx = (std::fabs(dx) >= min_step_px) ? (dx > 0.0 ? 1 : -1) : 0;
      prev_sy = (std::fabs(dy) >= min_step_px) ? (dy > 0.0 ? 1 : -1) : 0;
    }
    prev_dx = dx;
    prev_dy = dy;
    have_prev = true;
  }
  report.ok =
      report.direction_reversals == 0 && report.max_jerk_px <= max_jerk_px &&
      std::isfinite(report.max_step_px) && std::isfinite(report.max_jerk_px);
  return report;
}

} // namespace instantee::direction
