#pragma once

#include <instantee/core_model/camera.hpp>
#include <instantee/core_model/depth_stage.hpp>
#include <instantee/core_model/transform.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/camera_language.hpp>
#include <instantee/direction/directed_film.hpp>
#include <instantee/direction/motion_grammar.hpp>
#include <instantee/direction/shot_planner.hpp>
#include <instantee/direction/style_bible.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Curated Paper Cinema camera moves. Built on declared CameraCapability values
// (P4 camera language) and optional Motion Grammar `camera_move` timing —
// never on style-package name branches in render backends.
enum class CinematicCameraPreset : std::uint8_t {
  dolly = 1,  // axial truck along camera forward (-Z)
  truck,      // lateral move (translate_x)
  slow_zoom,  // gentle scale zoom
  crane_pan,  // rise (translate_y) + lateral pan
  rack_depth, // focus-pull approximation via small axial shift
};

struct CinematicCameraRequest {
  ShotPlan shot{};
  std::string camera_node_id;
  CinematicCameraPreset preset = CinematicCameraPreset::dolly;
  CameraCapabilitySet allowed = CameraCapabilitySet::all_v1();
  // Peak amplitude in [0, 1]. When grammar is set, multiplied by
  // intensity_scale.
  double intensity = 0.4;
  // Optional Motion Grammar binding. Must use GesturePattern::camera_move.
  std::optional<MotionGrammarBinding> grammar{};
};

// Capabilities each preset requires. Optional companions are omitted when the
// director has not declared them (same fail-closed rule as camera language).
[[nodiscard]] std::vector<CameraCapability>
required_capabilities_for_preset(CinematicCameraPreset preset);

[[nodiscard]] std::expected<std::vector<DirectedTrack>, diagnostics::Error>
plan_cinematic_camera_preset(const CinematicCameraRequest &request);

[[nodiscard]] constexpr std::string_view
cinematic_camera_preset_id(CinematicCameraPreset preset) noexcept {
  switch (preset) {
  case CinematicCameraPreset::dolly:
    return "dolly";
  case CinematicCameraPreset::truck:
    return "truck";
  case CinematicCameraPreset::slow_zoom:
    return "slow_zoom";
  case CinematicCameraPreset::crane_pan:
    return "crane_pan";
  case CinematicCameraPreset::rack_depth:
    return "rack_depth";
  }
  return "unknown";
}

[[nodiscard]] std::expected<CinematicCameraPreset, diagnostics::Error>
parse_cinematic_camera_preset(std::string_view text);

// --- Demo / framing stability helpers (CPU, no GPU) ---

struct CinematicSample {
  std::uint64_t frame = 0;
  core::LocalTransform camera_local{};
  // Screen position of a stage-plane anchor under the sampled camera.
  core::Vec3 anchor_pixel{};
};

// Ease unit interval with Style Bible MotionEase (spring ≈ ease_in_out here).
[[nodiscard]] double cinematic_ease(MotionEase ease, double t) noexcept;

// Interpolate a planned track at an absolute shot-relative frame using the
// request ease (defaults to ease_in_out). Outside the key window, clamps.
[[nodiscard]] std::expected<double, diagnostics::Error>
sample_directed_track_value(const DirectedTrack &track, std::uint64_t frame,
                            MotionEase ease = MotionEase::ease_in_out);

// Fold camera tracks into a LocalTransform at `frame` (identity defaults).
[[nodiscard]] std::expected<core::LocalTransform, diagnostics::Error>
sample_camera_local_from_tracks(const std::vector<DirectedTrack> &tracks,
                                std::uint64_t frame,
                                MotionEase ease = MotionEase::ease_in_out);

// Sample the same depth-stage scene under a preset plan. Stable framing means
// the projected subject moves smoothly (no sign-flipping jitter) and stays
// finite inside the viewport with bounded frame-to-frame jerk.
[[nodiscard]] std::expected<std::vector<CinematicSample>, diagnostics::Error>
sample_cinematic_preset_on_stage(const CinematicCameraRequest &request,
                                 const core::DepthStage &stage,
                                 std::size_t subject_plane_index,
                                 const core::PerspectiveFrustum &frustum,
                                 const core::Viewport &viewport);

struct FramingStabilityReport {
  bool ok = false;
  double max_step_px = 0;
  double max_jerk_px = 0;
  std::uint32_t direction_reversals = 0;
};

// Jitter heuristic: count axis direction reversals with material step size and
// compare peak jerk against a tolerance. Deterministic; no performance claim.
[[nodiscard]] FramingStabilityReport
measure_framing_stability(const std::vector<CinematicSample> &samples,
                          double min_step_px = 1e-4, double max_jerk_px = 8.0);

} // namespace instantee::direction
