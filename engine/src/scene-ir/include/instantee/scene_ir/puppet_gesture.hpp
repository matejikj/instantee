#pragma once

#include <instantee/core_model/transform.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/scene_ir/puppet_fk.hpp>
#include <instantee/scene_ir/puppet_rig.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace instantee::scene_ir {

// Semantic joint roles for retargetable gesture clips. Clips author against
// roles; a retarget map binds them to concrete joint ids on a given rig.
inline constexpr std::string_view kPuppetRoleRoot = "root";
inline constexpr std::string_view kPuppetRoleSpine = "spine";
inline constexpr std::string_view kPuppetRoleHead = "head";
inline constexpr std::string_view kPuppetRoleShoulderR = "shoulder_r";
inline constexpr std::string_view kPuppetRoleElbowR = "elbow_r";
inline constexpr std::string_view kPuppetRoleHandR = "hand_r";

struct PuppetGestureKeyframe {
  // Normalized clip time in [0, 1].
  double t = 0;
  // Local rotation deltas keyed by semantic role.
  std::unordered_map<std::string, PuppetJointPose> role_poses;
};

struct PuppetGestureClip {
  std::string id;
  double duration_frames = 30;
  std::vector<PuppetGestureKeyframe> keys;
};

// role_id → concrete joint_id on the target rig.
using PuppetGestureRetargetMap = std::unordered_map<std::string, std::string>;

struct PuppetGestureSample {
  std::string clip_id;
  double t = 0;
  std::unordered_map<std::string, PuppetJointPose> joint_poses;
};

[[nodiscard]] PuppetRigDocument make_reference_human_rig();

[[nodiscard]] PuppetGestureRetargetMap
make_reference_human_retarget_map() noexcept;

[[nodiscard]] std::vector<PuppetGestureClip> make_curated_gesture_library();

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_puppet_gesture_clip(const PuppetGestureClip &clip);

// Sample a clip at normalized time t (clamped to [0, 1]) and retarget role
// poses onto concrete joint ids. Missing retarget entries fail closed.
[[nodiscard]] std::expected<PuppetGestureSample, diagnostics::Error>
sample_puppet_gesture(const PuppetGestureClip &clip,
                      const PuppetGestureRetargetMap &retarget, double t);

// Evaluate FK on the reference (or any) rig using the sampled joint poses.
[[nodiscard]] std::expected<PuppetFkSkeleton, diagnostics::Error>
evaluate_puppet_gesture(const PuppetRigDocument &document,
                        const PuppetFkSkeleton &bind_skeleton,
                        const PuppetGestureSample &sample);

[[nodiscard]] std::string
format_puppet_gesture_golden(std::string_view label,
                             const PuppetGestureClip &clip,
                             const PuppetFkSkeleton &posed, double t);

} // namespace instantee::scene_ir
