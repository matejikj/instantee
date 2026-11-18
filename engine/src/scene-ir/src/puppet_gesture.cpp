#include <instantee/scene_ir/puppet_gesture.hpp>

#include <instantee/core_model/orientation.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace instantee::scene_ir {
namespace {

diagnostics::Error make_err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "puppet_gesture", message};
}

[[nodiscard]] double clamp01(double value) noexcept {
  if (!std::isfinite(value)) {
    return 0.0;
  }
  if (value < 0.0) {
    return 0.0;
  }
  if (value > 1.0) {
    return 1.0;
  }
  return value;
}

[[nodiscard]] PuppetJointPose lerp_pose(const PuppetJointPose &a,
                                        const PuppetJointPose &b, double t) {
  PuppetJointPose out;
  out.rotation_z_rad =
      core::lerp_angle_short(a.rotation_z_rad, b.rotation_z_rad, t);
  out.translate_delta = core::Vec3{
      a.translate_delta.x + (b.translate_delta.x - a.translate_delta.x) * t,
      a.translate_delta.y + (b.translate_delta.y - a.translate_delta.y) * t,
      a.translate_delta.z + (b.translate_delta.z - a.translate_delta.z) * t,
  };
  return out;
}

[[nodiscard]] PuppetGestureKeyframe
key(double t,
    std::initializer_list<std::pair<std::string_view, PuppetJointPose>> poses) {
  PuppetGestureKeyframe frame;
  frame.t = t;
  for (const auto &[role, pose] : poses) {
    frame.role_poses.emplace(std::string(role), pose);
  }
  return frame;
}

} // namespace

PuppetRigDocument make_reference_human_rig() {
  PuppetRigDocument doc;
  doc.schema_version = std::string{kPuppetRigSchemaVersion};
  doc.id = "reference-human";
  doc.retarget.profile = std::string{kPuppetRigRetargetProfile};
  doc.retarget.space = std::string{kPuppetRigRetargetSpace};
  doc.joints = {
      PuppetJoint{.id = "root", .pivot = {0, 0}},
      PuppetJoint{.id = "spine", .parent_id = "root", .pivot = {0, -36}},
      PuppetJoint{.id = "head", .parent_id = "spine", .pivot = {0, -70}},
      PuppetJoint{.id = "shoulder_r", .parent_id = "spine", .pivot = {28, -48}},
      PuppetJoint{
          .id = "elbow_r", .parent_id = "shoulder_r", .pivot = {58, -48}},
      PuppetJoint{.id = "hand_r", .parent_id = "elbow_r", .pivot = {88, -48}},
  };
  doc.constraints = {
      PuppetConstraint{.id = "elbow_r_hinge",
                       .type = PuppetConstraintType::hinge_limit,
                       .joint_id = "elbow_r",
                       .min_rad = -2.6,
                       .max_rad = 0.2},
  };
  return doc;
}

PuppetGestureRetargetMap make_reference_human_retarget_map() noexcept {
  return {
      {std::string(kPuppetRoleRoot), "root"},
      {std::string(kPuppetRoleSpine), "spine"},
      {std::string(kPuppetRoleHead), "head"},
      {std::string(kPuppetRoleShoulderR), "shoulder_r"},
      {std::string(kPuppetRoleElbowR), "elbow_r"},
      {std::string(kPuppetRoleHandR), "hand_r"},
  };
}

std::vector<PuppetGestureClip> make_curated_gesture_library() {
  using P = PuppetJointPose;
  std::vector<PuppetGestureClip> clips;

  clips.push_back(PuppetGestureClip{
      .id = "raise-hand",
      .duration_frames = 24,
      .keys =
          {
              key(0.0, {{kPuppetRoleShoulderR, P{}},
                        {kPuppetRoleElbowR, P{}},
                        {kPuppetRoleHandR, P{}}}),
              key(0.5, {{kPuppetRoleShoulderR, P{.rotation_z_rad = -1.1}},
                        {kPuppetRoleElbowR, P{.rotation_z_rad = -0.6}},
                        {kPuppetRoleHandR, P{.rotation_z_rad = -0.2}}}),
              key(1.0, {{kPuppetRoleShoulderR, P{.rotation_z_rad = -1.4}},
                        {kPuppetRoleElbowR, P{.rotation_z_rad = -0.35}},
                        {kPuppetRoleHandR, P{.rotation_z_rad = 0.0}}}),
          },
  });

  clips.push_back(PuppetGestureClip{
      .id = "point",
      .duration_frames = 20,
      .keys =
          {
              key(0.0, {{kPuppetRoleShoulderR, P{}},
                        {kPuppetRoleElbowR, P{}},
                        {kPuppetRoleHandR, P{}}}),
              key(1.0, {{kPuppetRoleShoulderR, P{.rotation_z_rad = -0.35}},
                        {kPuppetRoleElbowR, P{.rotation_z_rad = -0.15}},
                        {kPuppetRoleHandR, P{.rotation_z_rad = -0.1}}}),
          },
  });

  clips.push_back(PuppetGestureClip{
      .id = "grasp",
      .duration_frames = 18,
      .keys =
          {
              key(0.0, {{kPuppetRoleShoulderR, P{}},
                        {kPuppetRoleElbowR, P{}},
                        {kPuppetRoleHandR, P{}}}),
              key(1.0, {{kPuppetRoleShoulderR, P{.rotation_z_rad = -0.55}},
                        {kPuppetRoleElbowR, P{.rotation_z_rad = -1.4}},
                        {kPuppetRoleHandR, P{.rotation_z_rad = 0.35}}}),
          },
  });

  clips.push_back(PuppetGestureClip{
      .id = "look-at",
      .duration_frames = 16,
      .keys =
          {
              key(0.0, {{kPuppetRoleHead, P{}}, {kPuppetRoleSpine, P{}}}),
              key(1.0, {{kPuppetRoleHead, P{.rotation_z_rad = 0.4}},
                        {kPuppetRoleSpine, P{.rotation_z_rad = 0.08}}}),
          },
  });

  clips.push_back(PuppetGestureClip{
      .id = "nod",
      .duration_frames = 24,
      .keys =
          {
              key(0.0, {{kPuppetRoleHead, P{}}}),
              key(0.35, {{kPuppetRoleHead, P{.rotation_z_rad = 0.25}}}),
              key(0.7, {{kPuppetRoleHead, P{.rotation_z_rad = -0.15}}}),
              key(1.0, {{kPuppetRoleHead, P{}}}),
          },
  });

  return clips;
}

std::expected<void, diagnostics::Error>
validate_puppet_gesture_clip(const PuppetGestureClip &clip) {
  if (clip.id.empty()) {
    return std::unexpected(make_err("invalid_id", "Gesture clip id is empty"));
  }
  if (!(clip.duration_frames > 0.0) || !std::isfinite(clip.duration_frames)) {
    return std::unexpected(
        make_err("invalid_duration", "duration_frames must be finite and > 0"));
  }
  if (clip.keys.size() < 2U) {
    return std::unexpected(
        make_err("invalid_keys", "Gesture clip needs at least two keyframes"));
  }
  double prev_t = -1.0;
  for (const auto &frame : clip.keys) {
    if (!std::isfinite(frame.t) || frame.t < 0.0 || frame.t > 1.0) {
      return std::unexpected(
          make_err("invalid_time", "Keyframe t must be finite in [0, 1]"));
    }
    if (frame.t < prev_t) {
      return std::unexpected(
          make_err("invalid_time", "Keyframe times must be non-decreasing"));
    }
    prev_t = frame.t;
    if (frame.role_poses.empty()) {
      return std::unexpected(
          make_err("empty_poses", "Keyframe has no role poses"));
    }
    for (const auto &[role, pose] : frame.role_poses) {
      if (role.empty()) {
        return std::unexpected(make_err("invalid_id", "Empty role id"));
      }
      if (!std::isfinite(pose.rotation_z_rad) ||
          !std::isfinite(pose.translate_delta.x) ||
          !std::isfinite(pose.translate_delta.y) ||
          !std::isfinite(pose.translate_delta.z)) {
        return std::unexpected(
            make_err("non_finite", "Gesture pose values must be finite"));
      }
    }
  }
  if (!core::nearly_equal(clip.keys.front().t, 0.0) ||
      !core::nearly_equal(clip.keys.back().t, 1.0)) {
    return std::unexpected(
        make_err("invalid_time", "Gesture clips must start at 0 and end at 1"));
  }
  return {};
}

std::expected<PuppetGestureSample, diagnostics::Error>
sample_puppet_gesture(const PuppetGestureClip &clip,
                      const PuppetGestureRetargetMap &retarget, double t) {
  if (auto ok = validate_puppet_gesture_clip(clip); !ok) {
    return std::unexpected(ok.error());
  }
  const double tn = clamp01(t);

  std::size_t upper = 1;
  while (upper + 1 < clip.keys.size() && clip.keys[upper].t < tn) {
    ++upper;
  }
  const std::size_t lower = upper - 1;
  const auto &a = clip.keys[lower];
  const auto &b = clip.keys[upper];
  double local_t = 0.0;
  const double span = b.t - a.t;
  if (span > 1e-12) {
    local_t = (tn - a.t) / span;
  }

  std::unordered_map<std::string, PuppetJointPose> role_sample;
  // Union of roles present in either keyframe; missing side uses identity.
  std::vector<std::string> roles;
  roles.reserve(a.role_poses.size() + b.role_poses.size());
  for (const auto &[role, _] : a.role_poses) {
    roles.push_back(role);
  }
  for (const auto &[role, _] : b.role_poses) {
    if (std::find(roles.begin(), roles.end(), role) == roles.end()) {
      roles.push_back(role);
    }
  }

  PuppetGestureSample sample;
  sample.clip_id = clip.id;
  sample.t = tn;
  for (const auto &role : roles) {
    PuppetJointPose from{};
    PuppetJointPose to{};
    if (const auto it = a.role_poses.find(role); it != a.role_poses.end()) {
      from = it->second;
    }
    if (const auto it = b.role_poses.find(role); it != b.role_poses.end()) {
      to = it->second;
    }
    const auto pose = lerp_pose(from, to, local_t);

    const auto map_it = retarget.find(role);
    if (map_it == retarget.end()) {
      return std::unexpected(make_err(
          "missing_retarget", "Gesture role has no retarget joint mapping"));
    }
    if (map_it->second.empty()) {
      return std::unexpected(
          make_err("invalid_retarget", "Retarget joint id is empty"));
    }
    sample.joint_poses.emplace(map_it->second, pose);
  }
  return sample;
}

std::expected<PuppetFkSkeleton, diagnostics::Error>
evaluate_puppet_gesture(const PuppetRigDocument &document,
                        const PuppetFkSkeleton &bind_skeleton,
                        const PuppetGestureSample &sample) {
  PuppetFkSkeleton posed = bind_skeleton;
  if (auto ok = evaluate_puppet_fk(posed, document, sample.joint_poses); !ok) {
    return std::unexpected(ok.error());
  }
  return posed;
}

std::string format_puppet_gesture_golden(std::string_view label,
                                         const PuppetGestureClip &clip,
                                         const PuppetFkSkeleton &posed,
                                         double t) {
  std::ostringstream os;
  os << "puppet_gesture " << label << '\n';
  os << "clip=" << clip.id << " t=" << std::fixed << std::setprecision(4) << t
     << " joints=" << posed.joints.size() << '\n';
  for (const auto &joint : posed.joints) {
    const auto p = puppet_fk_world_position(joint);
    os << "j " << joint.id << " x=" << std::setprecision(6) << p.x
       << " y=" << p.y << " rz=" << joint.local.rotation_z_rad << '\n';
  }
  return os.str();
}

} // namespace instantee::scene_ir
