#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/safe_area.hpp>
#include <instantee/direction/transition_planner.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Editorial transition vocabulary built on the Transition Planner. Presets
// lower to planner intents; per-frame samples never invent frames beyond the
// packed film duration and must keep typographic focus inside safe areas.

enum class EditorialTransitionPreset : std::uint8_t {
  hard_cut = 1,
  crop_wipe = 2,
  rapid_zoom = 3,
  camera_snap = 4,
};

enum class TypographicEmphasis : std::uint8_t {
  none = 1,
  slam_in = 2,   // brief scale punch on the incoming type focus
  mask_push = 3, // opacity tracks wipe/zoom progress
};

struct EditorialTransitionRequest {
  std::string id;
  std::string from_shot_id;
  std::string to_shot_id;
  EditorialTransitionPreset preset = EditorialTransitionPreset::hard_cut;
  TypographicEmphasis emphasis = TypographicEmphasis::none;
  // Overlap/gesture length. Must be 0 for hard_cut; > 0 for the others.
  std::uint64_t duration_frames = 0;
  // Peak amplitude in (0, 1].
  double intensity = 0.75;
  SafeAreaPolicy safe_area{};
  double canvas_width_px = 0;
  double canvas_height_px = 0;
  // Important typography focus rect in canvas pixels (pre-transform).
  ContentSafeRect type_focus{};
};

struct EditorialTransitionProgram {
  TransitionIntent intent{};
  EditorialTransitionPreset preset = EditorialTransitionPreset::hard_cut;
  TypographicEmphasis emphasis = TypographicEmphasis::none;
  double intensity = 0.75;
  std::uint64_t duration_frames = 0;
  SafeAreaPolicy safe_area{};
  double canvas_width_px = 0;
  double canvas_height_px = 0;
  ContentSafeRect type_focus{};
};

struct EditorialTransitionFrame {
  std::uint64_t local_frame = 0;
  double progress = 0.0;
  double wipe_coverage = 0.0;
  double zoom_scale = 1.0;
  double snap_offset_x_px = 0.0;
  double snap_offset_y_px = 0.0;
  double type_scale = 1.0;
  double type_opacity = 1.0;
  ContentSafeRect type_bounds{};
  bool type_inside_safe_area = false;
};

struct EditorialTransitionVerification {
  TransitionPlan plan{};
  std::uint64_t packed_duration_frames = 0;
  bool duration_preserved = false;
  bool safe_areas_ok = false;
  std::vector<EditorialTransitionFrame> frames{};
};

[[nodiscard]] constexpr std::string_view
editorial_transition_preset_id(EditorialTransitionPreset preset) noexcept {
  switch (preset) {
  case EditorialTransitionPreset::hard_cut:
    return "hard_cut";
  case EditorialTransitionPreset::crop_wipe:
    return "crop_wipe";
  case EditorialTransitionPreset::rapid_zoom:
    return "rapid_zoom";
  case EditorialTransitionPreset::camera_snap:
    return "camera_snap";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view
typographic_emphasis_id(TypographicEmphasis emphasis) noexcept {
  switch (emphasis) {
  case TypographicEmphasis::none:
    return "none";
  case TypographicEmphasis::slam_in:
    return "slam_in";
  case TypographicEmphasis::mask_push:
    return "mask_push";
  }
  return "unknown";
}

// Compile a library request into a planner intent + sampling program.
[[nodiscard]] std::expected<EditorialTransitionProgram, diagnostics::Error>
compile_editorial_transition(const EditorialTransitionRequest &request);

// Sample one local frame inside the transition gesture (0 .. duration).
// For hard cuts duration is 0 and only local_frame 0 is valid.
[[nodiscard]] std::expected<EditorialTransitionFrame, diagnostics::Error>
sample_editorial_transition(const EditorialTransitionProgram &program,
                            std::uint64_t local_frame);

// Plan against packed shots and verify duration + safe-area invariants across
// every local frame of the gesture.
[[nodiscard]] std::expected<EditorialTransitionVerification, diagnostics::Error>
verify_editorial_transition(std::span<const ShotTiming> shots,
                            const EditorialTransitionProgram &program);

} // namespace instantee::direction
