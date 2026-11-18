#include <instantee/scene_ir/puppet_ik.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace instantee::scene_ir {
namespace {

diagnostics::Error make_err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "puppet_ik", message};
}

[[nodiscard]] bool finite_vec(const core::Vec3 &v) noexcept {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] double length2(const core::Vec3 &v) noexcept {
  return std::sqrt(v.x * v.x + v.y * v.y);
}

[[nodiscard]] double clamp_hinge(double value, double min_rad,
                                 double max_rad) noexcept {
  return std::min(max_rad, std::max(min_rad, value));
}

[[nodiscard]] std::expected<std::vector<std::size_t>, diagnostics::Error>
build_chain(const PuppetFkSkeleton &skeleton, std::string_view chain_root_id,
            std::string_view tip_id) {
  const auto tip_it = skeleton.index_by_id.find(std::string(tip_id));
  const auto root_it = skeleton.index_by_id.find(std::string(chain_root_id));
  if (tip_it == skeleton.index_by_id.end() ||
      root_it == skeleton.index_by_id.end()) {
    return std::unexpected(
        make_err("unknown_joint", "IK chain references unknown joint id"));
  }

  std::vector<std::size_t> reverse;
  reverse.reserve(skeleton.joints.size());
  std::size_t cursor = tip_it->second;
  const std::size_t root_index = root_it->second;
  while (true) {
    reverse.push_back(cursor);
    if (cursor == root_index) {
      break;
    }
    const auto &joint = skeleton.joints[cursor];
    if (!joint.parent_index.has_value()) {
      return std::unexpected(make_err(
          "invalid_chain", "Tip is not a descendant of the chain root"));
    }
    cursor = *joint.parent_index;
    if (reverse.size() > skeleton.joints.size()) {
      return std::unexpected(
          make_err("cyclic_parent", "IK chain walk exceeded joint count"));
    }
  }

  std::reverse(reverse.begin(), reverse.end());
  // Two segments ⇒ at least three joints (root, mid, tip).
  if (reverse.size() < 3U) {
    return std::unexpected(make_err(
        "invalid_chain", "IK chain requires at least two bone segments"));
  }
  return reverse;
}

[[nodiscard]] std::expected<void, diagnostics::Error>
sanitize_poses(const std::unordered_map<std::string, PuppetJointPose> &poses) {
  for (const auto &[id, pose] : poses) {
    if (!std::isfinite(pose.rotation_z_rad) ||
        !finite_vec(pose.translate_delta)) {
      return std::unexpected(
          make_err("non_finite", "IK produced a non-finite pose"));
    }
  }
  return {};
}

} // namespace

std::expected<PuppetIkResult, diagnostics::Error>
solve_puppet_ik_2d(const PuppetRigDocument &document,
                   const PuppetFkSkeleton &skeleton,
                   std::string_view chain_root_id, std::string_view tip_id,
                   core::Vec3 target, PuppetIkOptions options) {
  if (skeleton.rig_id != document.id ||
      skeleton.joints.size() != document.joints.size()) {
    return std::unexpected(make_err(
        "rig_mismatch", "Skeleton does not match puppet-rig document"));
  }
  if (!finite_vec(target)) {
    return std::unexpected(make_err("non_finite", "IK target must be finite"));
  }
  if (options.max_iterations == 0U) {
    return std::unexpected(
        make_err("invalid_options", "max_iterations must be positive"));
  }
  if (!(options.tolerance_px >= 0.0) || !std::isfinite(options.tolerance_px)) {
    return std::unexpected(
        make_err("invalid_options", "tolerance_px must be finite and >= 0"));
  }

  auto chain = build_chain(skeleton, chain_root_id, tip_id);
  if (!chain) {
    return std::unexpected(chain.error());
  }

  std::unordered_map<std::string, std::pair<double, double>> hinges;
  if (options.apply_hinge_limits) {
    for (const auto &constraint : document.constraints) {
      if (constraint.type != PuppetConstraintType::hinge_limit) {
        continue;
      }
      if (!constraint.min_rad.has_value() || !constraint.max_rad.has_value() ||
          !std::isfinite(*constraint.min_rad) ||
          !std::isfinite(*constraint.max_rad) ||
          *constraint.min_rad > *constraint.max_rad) {
        return std::unexpected(make_err(
            "invalid_hinge", "hinge_limit requires finite min_rad <= max_rad"));
      }
      hinges.emplace(constraint.joint_id,
                     std::pair{*constraint.min_rad, *constraint.max_rad});
    }
  }

  // Fixed bone lengths from bind offsets of child joints in the chain.
  double max_reach = 0.0;
  for (std::size_t i = 1; i < chain->size(); ++i) {
    const double len = length2(skeleton.joints[(*chain)[i]].bind_offset);
    if (!(len > 0.0) || !std::isfinite(len)) {
      return std::unexpected(make_err(
          "invalid_bone", "IK bone length must be positive and finite"));
    }
    max_reach += len;
  }

  PuppetFkSkeleton working = skeleton;
  std::unordered_map<std::string, PuppetJointPose> poses;
  poses.reserve(chain->size());
  for (std::size_t index : *chain) {
    poses.emplace(working.joints[index].id, PuppetJointPose{});
  }

  // Seed bind pose world matrices.
  if (auto ok = evaluate_puppet_fk(
          working, document, poses,
          PuppetFkEvaluateOptions{.apply_hinge_limits =
                                      options.apply_hinge_limits});
      !ok) {
    return std::unexpected(ok.error());
  }

  const core::Vec3 root_pos =
      puppet_fk_world_position(working.joints[(*chain)[0]]);
  core::Vec3 solved_target = target;
  bool used_projection = false;
  const double root_to_target =
      length2(core::Vec3{target.x - root_pos.x, target.y - root_pos.y, 0.0});
  if (root_to_target > max_reach && max_reach > 0.0) {
    used_projection = true;
    const double scale = max_reach / root_to_target;
    solved_target = core::Vec3{
        root_pos.x + (target.x - root_pos.x) * scale,
        root_pos.y + (target.y - root_pos.y) * scale,
        0.0,
    };
  }

  PuppetIkResult result;
  result.used_reach_projection = used_projection;
  result.solved_target = solved_target;

  auto tip_error = [&]() {
    const auto tip = puppet_fk_world_position(working.joints[chain->back()]);
    return length2(
        core::Vec3{tip.x - solved_target.x, tip.y - solved_target.y, 0.0});
  };

  result.tip_error_px = tip_error();
  if (result.tip_error_px <= options.tolerance_px) {
    result.status = used_projection ? PuppetIkStatus::fallback_unreachable
                                    : PuppetIkStatus::reached;
    result.iterations = 0;
    result.poses = std::move(poses);
    if (auto ok = sanitize_poses(result.poses); !ok) {
      return std::unexpected(ok.error());
    }
    return result;
  }

  for (std::uint32_t iter = 1; iter <= options.max_iterations; ++iter) {
    // CCD: rotate each joint from tip-parent toward chain root.
    for (std::size_t ci = chain->size() - 1; ci-- > 0;) {
      const std::size_t joint_index = (*chain)[ci];
      auto &joint = working.joints[joint_index];
      const core::Vec3 joint_pos = puppet_fk_world_position(joint);
      const core::Vec3 tip_pos =
          puppet_fk_world_position(working.joints[chain->back()]);

      const core::Vec3 to_tip{tip_pos.x - joint_pos.x, tip_pos.y - joint_pos.y,
                              0.0};
      const core::Vec3 to_tgt{solved_target.x - joint_pos.x,
                              solved_target.y - joint_pos.y, 0.0};
      const double len_tip = length2(to_tip);
      const double len_tgt = length2(to_tgt);
      if (!(len_tip > 1e-12) || !(len_tgt > 1e-12)) {
        continue;
      }
      const double dot =
          (to_tip.x * to_tgt.x + to_tip.y * to_tgt.y) / (len_tip * len_tgt);
      const double cross =
          (to_tip.x * to_tgt.y - to_tip.y * to_tgt.x) / (len_tip * len_tgt);
      const double clamped_dot = std::min(1.0, std::max(-1.0, dot));
      double delta = std::atan2(cross, clamped_dot);
      if (!std::isfinite(delta)) {
        return std::unexpected(
            make_err("non_finite", "IK angle delta was non-finite"));
      }

      PuppetJointPose &pose = poses[joint.id];
      pose.rotation_z_rad += delta;
      if (options.apply_hinge_limits) {
        if (const auto hit = hinges.find(joint.id); hit != hinges.end()) {
          pose.rotation_z_rad = clamp_hinge(
              pose.rotation_z_rad, hit->second.first, hit->second.second);
        }
      }
      if (!std::isfinite(pose.rotation_z_rad)) {
        return std::unexpected(
            make_err("non_finite", "IK rotation became non-finite"));
      }

      if (auto ok = evaluate_puppet_fk(
              working, document, poses,
              PuppetFkEvaluateOptions{.apply_hinge_limits =
                                          options.apply_hinge_limits});
          !ok) {
        return std::unexpected(ok.error());
      }
    }

    result.iterations = iter;
    result.tip_error_px = tip_error();
    if (!std::isfinite(result.tip_error_px)) {
      return std::unexpected(
          make_err("non_finite", "IK tip error became non-finite"));
    }
    if (result.tip_error_px <= options.tolerance_px) {
      result.status = used_projection ? PuppetIkStatus::fallback_unreachable
                                      : PuppetIkStatus::reached;
      result.poses = std::move(poses);
      if (auto ok = sanitize_poses(result.poses); !ok) {
        return std::unexpected(ok.error());
      }
      return result;
    }
  }

  result.status = used_projection ? PuppetIkStatus::fallback_unreachable
                                  : PuppetIkStatus::iteration_cap;
  result.poses = std::move(poses);
  if (auto ok = sanitize_poses(result.poses); !ok) {
    return std::unexpected(ok.error());
  }
  return result;
}

} // namespace instantee::scene_ir
