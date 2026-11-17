#include <instantee/direction/camera_language.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_error(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "camera_language", message};
}

[[nodiscard]] constexpr std::uint8_t bit_for(CameraCapability cap) noexcept {
  return static_cast<std::uint8_t>(1u << (static_cast<std::uint8_t>(cap) - 1u));
}

struct ActionWindow {
  std::uint64_t start = 0;
  std::uint64_t end = 0; // inclusive keyframe frame
};

[[nodiscard]] ActionWindow action_window(const ShotPlan &shot) noexcept {
  for (const ShotSubevent &ev : shot.subevents) {
    if (ev.kind == ShotSubeventKind::action && ev.duration_frames > 0) {
      const std::uint64_t last = ev.start_frame + ev.duration_frames - 1;
      return ActionWindow{.start = ev.start_frame, .end = last};
    }
  }
  if (shot.duration_frames == 0) {
    return {};
  }
  return ActionWindow{.start = 0, .end = shot.duration_frames - 1};
}

struct MoveSpec {
  CameraCapability capability = CameraCapability::dolly;
  bool required = true;
  std::string_view property;
  double from = 0.0;
  double to = 0.0;
};

// Camera forward is -Z (shared camera contract): push_in dollys toward the
// subject (-Z), pull_out retreats (+Z). Zoom is an optional companion move.
[[nodiscard]] std::vector<MoveSpec> moves_for(CameraFraming framing,
                                              double intensity) {
  const double peak = std::clamp(intensity, 0.0, 1.0);
  std::vector<MoveSpec> moves;
  switch (framing) {
  case CameraFraming::static_framing:
    break;
  case CameraFraming::push_in:
    moves.push_back({.capability = CameraCapability::dolly,
                     .required = true,
                     .property = "translate_z",
                     .from = 0.0,
                     .to = -40.0 * peak});
    moves.push_back({.capability = CameraCapability::zoom,
                     .required = false,
                     .property = "scale_x",
                     .from = 1.0,
                     .to = 1.0 + 0.12 * peak});
    moves.push_back({.capability = CameraCapability::zoom,
                     .required = false,
                     .property = "scale_y",
                     .from = 1.0,
                     .to = 1.0 + 0.12 * peak});
    break;
  case CameraFraming::pull_out:
    moves.push_back({.capability = CameraCapability::dolly,
                     .required = true,
                     .property = "translate_z",
                     .from = 0.0,
                     .to = 40.0 * peak});
    moves.push_back({.capability = CameraCapability::zoom,
                     .required = false,
                     .property = "scale_x",
                     .from = 1.0,
                     .to = 1.0 - 0.08 * peak});
    moves.push_back({.capability = CameraCapability::zoom,
                     .required = false,
                     .property = "scale_y",
                     .from = 1.0,
                     .to = 1.0 - 0.08 * peak});
    break;
  case CameraFraming::pan:
    moves.push_back({.capability = CameraCapability::pan,
                     .required = true,
                     .property = "translate_x",
                     .from = 0.0,
                     .to = 48.0 * peak});
    break;
  case CameraFraming::orbit:
    moves.push_back({.capability = CameraCapability::pan,
                     .required = true,
                     .property = "translate_x",
                     .from = -24.0 * peak,
                     .to = 24.0 * peak});
    moves.push_back({.capability = CameraCapability::dolly,
                     .required = true,
                     .property = "translate_z",
                     .from = 0.0,
                     .to = -20.0 * peak});
    moves.push_back({.capability = CameraCapability::focus,
                     .required = false,
                     .property = "rotation_z_rad",
                     .from = 0.0,
                     .to = 0.18 * peak});
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
            "Director requested a camera move outside declared capabilities"));
      }
      continue;
    }
    selected.push_back(move);
  }
  return selected;
}

} // namespace

CameraCapabilitySet CameraCapabilitySet::all_v1() {
  CameraCapabilitySet set;
  set.allow(CameraCapability::dolly);
  set.allow(CameraCapability::pan);
  set.allow(CameraCapability::zoom);
  set.allow(CameraCapability::focus);
  return set;
}

bool CameraCapabilitySet::contains(CameraCapability capability) const noexcept {
  return (mask_ & bit_for(capability)) != 0;
}

void CameraCapabilitySet::allow(CameraCapability capability) noexcept {
  mask_ = static_cast<std::uint8_t>(mask_ | bit_for(capability));
}

void CameraCapabilitySet::revoke(CameraCapability capability) noexcept {
  mask_ = static_cast<std::uint8_t>(
      mask_ & static_cast<std::uint8_t>(~bit_for(capability)));
}

std::vector<CameraCapability> CameraCapabilitySet::list() const {
  std::vector<CameraCapability> out;
  for (CameraCapability cap :
       {CameraCapability::dolly, CameraCapability::pan, CameraCapability::zoom,
        CameraCapability::focus}) {
    if (contains(cap)) {
      out.push_back(cap);
    }
  }
  return out;
}

std::vector<CameraCapability> required_capabilities_for(CameraFraming framing) {
  std::vector<CameraCapability> required;
  const auto moves = moves_for(framing, 1.0);
  for (const MoveSpec &move : moves) {
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

std::expected<CameraCapability, diagnostics::Error>
parse_camera_capability(std::string_view text) {
  if (text == "dolly") {
    return CameraCapability::dolly;
  }
  if (text == "pan") {
    return CameraCapability::pan;
  }
  if (text == "zoom") {
    return CameraCapability::zoom;
  }
  if (text == "focus") {
    return CameraCapability::focus;
  }
  return std::unexpected(
      make_error("unknown_camera_capability", "Unknown camera capability id"));
}

std::expected<std::vector<DirectedTrack>, diagnostics::Error>
plan_camera_language(const CameraLanguageRequest &request) {
  if (request.camera_node_id.empty()) {
    return std::unexpected(
        make_error("invalid_camera_node", "Camera node id must be non-empty"));
  }
  if (request.shot.duration_frames == 0 || request.shot.shot_id.empty()) {
    return std::unexpected(make_error(
        "invalid_shot_plan", "Camera language requires a planned shot"));
  }
  if (!std::isfinite(request.intensity) || request.intensity < 0.0 ||
      request.intensity > 1.0) {
    return std::unexpected(
        make_error("invalid_intensity", "Intensity must be finite in [0, 1]"));
  }

  const auto candidates = moves_for(request.shot.framing, request.intensity);
  auto selected = select_allowed_moves(request.allowed, candidates);
  if (!selected) {
    return std::unexpected(selected.error());
  }

  if (selected->empty()) {
    return std::vector<DirectedTrack>{};
  }

  const ActionWindow window = action_window(request.shot);
  if (window.end < window.start || window.end >= request.shot.duration_frames) {
    return std::unexpected(
        make_error("invalid_action_window",
                   "Shot action window is outside the author duration budget"));
  }

  std::vector<DirectedTrack> tracks;
  tracks.reserve(selected->size());
  for (std::size_t i = 0; i < selected->size(); ++i) {
    const MoveSpec &spec = (*selected)[i];
    DirectedTrack track;
    track.id = request.shot.shot_id + "_cam_" +
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
      // Single-frame shot: publish the destination value.
      track.keyframes.front().value = spec.to;
    }
    tracks.push_back(std::move(track));
  }
  return tracks;
}

} // namespace instantee::direction
