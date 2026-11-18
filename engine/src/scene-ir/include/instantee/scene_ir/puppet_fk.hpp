#pragma once

#include <instantee/core_model/transform.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/scene_ir/puppet_rig.hpp>

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace instantee::scene_ir {

// Per-joint animated deltas applied on top of the bind local transform.
// Omitted joints keep bind rotation (0) and bind translation.
struct PuppetJointPose {
  double rotation_z_rad = 0;
  // Additive parent-space translation on top of the bind rest offset.
  core::Vec3 translate_delta{};
};

struct PuppetFkJointState {
  std::string id;
  std::optional<std::size_t> parent_index;
  // Bind-space rest pivot from the interchange document.
  core::Vec3 bind_pivot{};
  // Rest offset in parent space (root: bind pivot from world origin).
  core::Vec3 bind_offset{};
  core::LocalTransform local{};
  core::Mat4 local_matrix = core::Mat4::identity();
  core::Mat4 world_matrix = core::Mat4::identity();
};

// Parents precede children. Index map covers every joint id exactly once.
struct PuppetFkSkeleton {
  std::string rig_id;
  std::vector<PuppetFkJointState> joints;
  std::unordered_map<std::string, std::size_t> index_by_id;
};

struct PuppetFkEvaluateOptions {
  bool apply_hinge_limits = true;
};

// Build an ordered skeleton from a validated puppet-rig document. Joint pivots
// are interpreted in a shared 2D bind space; local bind translation is
// child_pivot - parent_pivot (SAS §8.1 local → parent → world).
[[nodiscard]] std::expected<PuppetFkSkeleton, diagnostics::Error>
build_puppet_fk_skeleton(const PuppetRigDocument &document);

// Propagate local poses into world matrices. Unknown pose keys fail closed.
// Hinge limits from the source document may clamp rotation_z when enabled.
[[nodiscard]] std::expected<void, diagnostics::Error> evaluate_puppet_fk(
    PuppetFkSkeleton &skeleton, const PuppetRigDocument &document,
    const std::unordered_map<std::string, PuppetJointPose> &poses = {},
    PuppetFkEvaluateOptions options = {});

[[nodiscard]] inline core::Vec3
puppet_fk_world_position(const PuppetFkJointState &joint) noexcept {
  return core::transform_point(joint.world_matrix, core::Vec3{0, 0, 0});
}

} // namespace instantee::scene_ir
