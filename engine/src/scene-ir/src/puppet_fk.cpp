#include <instantee/scene_ir/puppet_fk.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace instantee::scene_ir {
namespace {

diagnostics::Error make_err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "puppet_fk", message};
}

[[nodiscard]] bool finite_vec(const core::Vec3 &v) noexcept {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] double clamp_hinge(double value, double min_rad,
                                 double max_rad) noexcept {
  if (value < min_rad) {
    return min_rad;
  }
  if (value > max_rad) {
    return max_rad;
  }
  return value;
}

} // namespace

std::expected<PuppetFkSkeleton, diagnostics::Error>
build_puppet_fk_skeleton(const PuppetRigDocument &document) {
  if (auto ok = validate_puppet_rig(document); !ok) {
    return std::unexpected(ok.error());
  }

  const std::size_t count = document.joints.size();
  std::unordered_map<std::string, std::size_t> source_index;
  source_index.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    source_index.emplace(document.joints[i].id, i);
  }

  std::vector<std::size_t> indegree(count, 0);
  std::vector<std::vector<std::size_t>> children(count);
  std::optional<std::size_t> root_index;
  for (std::size_t i = 0; i < count; ++i) {
    const auto &joint = document.joints[i];
    if (!joint.parent_id.has_value()) {
      root_index = i;
      continue;
    }
    const auto parent_it = source_index.find(*joint.parent_id);
    if (parent_it == source_index.end()) {
      return std::unexpected(
          make_err("dangling_ref", "Joint parent_id is unknown"));
    }
    children[parent_it->second].push_back(i);
    ++indegree[i];
  }
  if (!root_index.has_value()) {
    return std::unexpected(
        make_err("invalid_hierarchy", "Rig has no root joint"));
  }

  // Kahn order: parents before children; siblings keep document order.
  std::vector<std::size_t> ready;
  ready.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    if (indegree[i] == 0U) {
      ready.push_back(i);
    }
  }
  std::vector<std::size_t> order;
  order.reserve(count);
  std::size_t cursor = 0;
  while (cursor < ready.size()) {
    const std::size_t node = ready[cursor++];
    order.push_back(node);
    for (std::size_t child : children[node]) {
      if (--indegree[child] == 0U) {
        ready.push_back(child);
      }
    }
  }
  if (order.size() != count) {
    return std::unexpected(
        make_err("cyclic_parent", "Joint hierarchy contains a cycle"));
  }

  PuppetFkSkeleton skeleton;
  skeleton.rig_id = document.id;
  skeleton.joints.resize(count);
  skeleton.index_by_id.reserve(count);

  std::vector<std::size_t> fk_index_of_source(count, 0);
  for (std::size_t fk = 0; fk < order.size(); ++fk) {
    fk_index_of_source[order[fk]] = fk;
  }

  for (std::size_t fk = 0; fk < order.size(); ++fk) {
    const std::size_t src = order[fk];
    const auto &joint = document.joints[src];
    PuppetFkJointState state;
    state.id = joint.id;
    state.bind_pivot = core::Vec3{joint.pivot.x, joint.pivot.y, 0.0};
    if (!finite_vec(state.bind_pivot)) {
      return std::unexpected(
          make_err("non_finite", "Joint pivot must be finite"));
    }
    if (joint.parent_id.has_value()) {
      const std::size_t parent_src = source_index[*joint.parent_id];
      state.parent_index = fk_index_of_source[parent_src];
      const auto &parent = document.joints[parent_src];
      state.bind_offset = core::Vec3{joint.pivot.x - parent.pivot.x,
                                     joint.pivot.y - parent.pivot.y, 0.0};
    } else {
      state.parent_index = std::nullopt;
      state.bind_offset = state.bind_pivot;
    }
    if (!finite_vec(state.bind_offset)) {
      return std::unexpected(
          make_err("non_finite", "Bind offset must be finite"));
    }
    state.local = core::LocalTransform{
        .translate = state.bind_offset,
        .scale = {1, 1, 1},
        .rotation_z_rad = 0,
        .pivot = {0, 0, 0},
    };
    state.local_matrix = core::local_matrix(state.local);
    state.world_matrix = core::Mat4::identity();
    skeleton.index_by_id.emplace(state.id, fk);
    skeleton.joints[fk] = std::move(state);
  }

  // Seed world matrices from bind pose.
  for (auto &joint : skeleton.joints) {
    if (!joint.parent_index.has_value()) {
      joint.world_matrix = joint.local_matrix;
    } else {
      joint.world_matrix = core::combine_parent_local(
          skeleton.joints[*joint.parent_index].world_matrix,
          joint.local_matrix);
    }
  }
  return skeleton;
}

std::expected<void, diagnostics::Error> evaluate_puppet_fk(
    PuppetFkSkeleton &skeleton, const PuppetRigDocument &document,
    const std::unordered_map<std::string, PuppetJointPose> &poses,
    PuppetFkEvaluateOptions options) {
  if (skeleton.joints.size() != document.joints.size() ||
      skeleton.rig_id != document.id) {
    return std::unexpected(make_err(
        "rig_mismatch", "Skeleton does not match puppet-rig document"));
  }
  for (const auto &[id, pose] : poses) {
    if (!skeleton.index_by_id.contains(id)) {
      return std::unexpected(
          make_err("unknown_joint", "Pose references unknown joint id"));
    }
    if (!std::isfinite(pose.rotation_z_rad) ||
        !finite_vec(pose.translate_delta)) {
      return std::unexpected(
          make_err("non_finite", "Pose values must be finite"));
    }
  }

  std::unordered_map<std::string, std::pair<double, double>> hinges;
  if (options.apply_hinge_limits) {
    for (const auto &constraint : document.constraints) {
      if (constraint.type != PuppetConstraintType::hinge_limit) {
        continue;
      }
      if (!constraint.min_rad.has_value() || !constraint.max_rad.has_value()) {
        return std::unexpected(make_err(
            "missing_field", "hinge_limit requires min_rad and max_rad"));
      }
      hinges.emplace(constraint.joint_id,
                     std::pair{*constraint.min_rad, *constraint.max_rad});
    }
  }

  for (auto &joint : skeleton.joints) {
    PuppetJointPose pose{};
    if (const auto it = poses.find(joint.id); it != poses.end()) {
      pose = it->second;
    }
    double rotation = pose.rotation_z_rad;
    if (options.apply_hinge_limits) {
      if (const auto hit = hinges.find(joint.id); hit != hinges.end()) {
        rotation = clamp_hinge(rotation, hit->second.first, hit->second.second);
      }
    }
    joint.local = core::LocalTransform{
        .translate = core::Vec3{joint.bind_offset.x + pose.translate_delta.x,
                                joint.bind_offset.y + pose.translate_delta.y,
                                joint.bind_offset.z + pose.translate_delta.z},
        .scale = {1, 1, 1},
        .rotation_z_rad = rotation,
        .pivot = {0, 0, 0},
    };
    joint.local_matrix = core::local_matrix(joint.local);
  }

  for (auto &joint : skeleton.joints) {
    if (!joint.parent_index.has_value()) {
      joint.world_matrix = joint.local_matrix;
      continue;
    }
    if (*joint.parent_index >= skeleton.joints.size()) {
      return std::unexpected(
          make_err("dangling_ref", "parent_index out of range"));
    }
    // Parents precede children in skeleton.joints, so parent world is final.
    joint.world_matrix = core::combine_parent_local(
        skeleton.joints[*joint.parent_index].world_matrix, joint.local_matrix);
  }

  // Consistency: every non-root world equals parent_world * local.
  for (const auto &joint : skeleton.joints) {
    if (!joint.parent_index.has_value()) {
      continue;
    }
    const auto expected = core::combine_parent_local(
        skeleton.joints[*joint.parent_index].world_matrix, joint.local_matrix);
    if (!core::nearly_equal(expected, joint.world_matrix)) {
      return std::unexpected(make_err(
          "inconsistent_rig", "World matrix does not match parent*local"));
    }
  }
  return {};
}

} // namespace instantee::scene_ir
