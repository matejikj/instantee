#pragma once

#include <instantee/core_model/transform.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/scene_ir/puppet_fk.hpp>
#include <instantee/scene_ir/puppet_rig.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>

namespace instantee::scene_ir {

// Semantic roles for the biped walk retarget profile (paper_2d_v1 side view).
inline constexpr std::string_view kPuppetWalkRoleRoot = "root";
inline constexpr std::string_view kPuppetWalkRoleHipL = "hip_l";
inline constexpr std::string_view kPuppetWalkRoleKneeL = "knee_l";
inline constexpr std::string_view kPuppetWalkRoleFootL = "foot_l";
inline constexpr std::string_view kPuppetWalkRoleHipR = "hip_r";
inline constexpr std::string_view kPuppetWalkRoleKneeR = "knee_r";
inline constexpr std::string_view kPuppetWalkRoleFootR = "foot_r";

enum class PuppetLocomotionPhase : std::uint8_t {
  idle = 0,
  start,
  cycle,
  stop,
};

struct PuppetWalkTiming {
  // Frames for one full left/right gait cycle while cruising.
  double cycle_frames = 24.0;
  double start_frames = 8.0;
  double stop_frames = 8.0;
  // Canvas +X travel of the root per cruise cycle (px).
  double stride_px = 48.0;
  // Peak planted-foot lift during swing (canvas −Y is up).
  double step_height_px = 10.0;
  // Max |Δworld_x| of a planted foot across consecutive samples.
  double foot_slide_tol_px = 0.75;
};

// Distance-driven plan. Total locomotion covers travel_distance_px with
// ease-in start, repeating cycles, and ease-out stop. Evaluation is a pure
// function of absolute frame index (SAS §7.1 random-access).
struct PuppetWalkPlan {
  double travel_distance_px = 144.0;
  PuppetWalkTiming timing{};
};

using PuppetWalkRetargetMap = std::unordered_map<std::string, std::string>;

struct PuppetWalkFootLock {
  std::string foot_id;
  double plant_world_x = 0;
  bool locked = false;
};

struct PuppetWalkSample {
  PuppetLocomotionPhase phase = PuppetLocomotionPhase::idle;
  double frame = 0;
  // Continuous cycle units: floor = completed cycles, frac = cycle_t in [0, 1).
  double cycle_units = 0;
  double cycle_t = 0;
  double root_travel_x = 0;
  std::unordered_map<std::string, PuppetJointPose> joint_poses;
  PuppetWalkFootLock planted{};
};

[[nodiscard]] PuppetRigDocument make_biped_walk_rig();

[[nodiscard]] PuppetWalkRetargetMap make_biped_walk_retarget_map() noexcept;

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_puppet_walk_plan(const PuppetWalkPlan &plan);

// Total frame span implied by the plan (start + cruise + stop).
[[nodiscard]] std::expected<double, diagnostics::Error>
puppet_walk_total_frames(const PuppetWalkPlan &plan);

// Sample locomotion at an absolute frame. Out-of-range frames clamp to the
// terminal idle pose after stop. Missing retarget roles fail closed.
[[nodiscard]] std::expected<PuppetWalkSample, diagnostics::Error>
sample_puppet_walk(const PuppetWalkPlan &plan,
                   const PuppetWalkRetargetMap &retarget, double frame);

[[nodiscard]] std::expected<PuppetFkSkeleton, diagnostics::Error>
evaluate_puppet_walk(const PuppetRigDocument &document,
                     const PuppetFkSkeleton &bind_skeleton,
                     const PuppetWalkSample &sample);

// Max absolute world-X drift of the planted foot across consecutive integer
// frames in [frame_begin, frame_end]. Fails when any sample cannot be posed.
[[nodiscard]] std::expected<double, diagnostics::Error>
measure_puppet_walk_foot_slide(const PuppetRigDocument &document,
                               const PuppetFkSkeleton &bind_skeleton,
                               const PuppetWalkPlan &plan,
                               const PuppetWalkRetargetMap &retarget,
                               double frame_begin, double frame_end);

[[nodiscard]] std::string
format_puppet_walk_golden(std::string_view label,
                          const PuppetWalkSample &sample,
                          const PuppetFkSkeleton &posed, double foot_slide_px);

} // namespace instantee::scene_ir
