#include <instantee/scene_ir/puppet_reference_pack.hpp>

#include <iomanip>
#include <sstream>
#include <utility>

namespace instantee::scene_ir {
namespace {

diagnostics::Error make_err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "puppet_reference_pack", message};
}

[[nodiscard]] PuppetRigDocument make_historical_figure_rig() {
  // Gesture-capable upper body (same topology as the curated reference human)
  // plus painted paper layers / face parts for expressions and skin bindings.
  auto doc = make_reference_human_rig();
  doc.id = "historical-paper-figure";
  doc.bones = {
      PuppetBone{
          .id = "bone_spine", .from_joint_id = "root", .to_joint_id = "spine"},
      PuppetBone{
          .id = "bone_head", .from_joint_id = "spine", .to_joint_id = "head"},
      PuppetBone{.id = "bone_shoulder_r",
                 .from_joint_id = "spine",
                 .to_joint_id = "shoulder_r"},
      PuppetBone{.id = "bone_elbow_r",
                 .from_joint_id = "shoulder_r",
                 .to_joint_id = "elbow_r"},
      PuppetBone{.id = "bone_hand_r",
                 .from_joint_id = "elbow_r",
                 .to_joint_id = "hand_r"},
  };
  doc.layers = {
      PuppetLayer{.id = "torso_layer", .bind_joint_id = "spine"},
      PuppetLayer{.id = "head_layer", .bind_joint_id = "head"},
      PuppetLayer{.id = std::string(kPuppetPartHead), .bind_joint_id = "head"},
      PuppetLayer{.id = std::string(kPuppetPartEyeL), .bind_joint_id = "head"},
      PuppetLayer{.id = std::string(kPuppetPartEyeR), .bind_joint_id = "head"},
      PuppetLayer{.id = std::string(kPuppetPartMouth), .bind_joint_id = "head"},
      PuppetLayer{.id = "forearm_r_layer", .bind_joint_id = "elbow_r"},
  };
  doc.skin_bindings = {
      PuppetSkinBinding{.layer_id = "torso_layer",
                        .influences = {PuppetSkinInfluence{"spine", 0.75},
                                       PuppetSkinInfluence{"root", 0.25}}},
      PuppetSkinBinding{.layer_id = "head_layer",
                        .influences = {PuppetSkinInfluence{"head", 1.0}}},
      PuppetSkinBinding{.layer_id = std::string(kPuppetPartHead),
                        .influences = {PuppetSkinInfluence{"head", 1.0}}},
      PuppetSkinBinding{.layer_id = std::string(kPuppetPartEyeL),
                        .influences = {PuppetSkinInfluence{"head", 1.0}}},
      PuppetSkinBinding{.layer_id = std::string(kPuppetPartEyeR),
                        .influences = {PuppetSkinInfluence{"head", 1.0}}},
      PuppetSkinBinding{.layer_id = std::string(kPuppetPartMouth),
                        .influences = {PuppetSkinInfluence{"head", 1.0}}},
      PuppetSkinBinding{.layer_id = "forearm_r_layer",
                        .influences = {PuppetSkinInfluence{"elbow_r", 0.7},
                                       PuppetSkinInfluence{"hand_r", 0.3}}},
  };
  doc.reference_poses = {
      PuppetReferencePose{
          .id = "bind",
          .joints =
              {
                  PuppetPoseJoint{.joint_id = "root", .rotation_z_rad = 0},
                  PuppetPoseJoint{.joint_id = "spine", .rotation_z_rad = 0},
                  PuppetPoseJoint{.joint_id = "head", .rotation_z_rad = 0},
                  PuppetPoseJoint{.joint_id = "shoulder_r",
                                  .rotation_z_rad = 0},
                  PuppetPoseJoint{.joint_id = "elbow_r", .rotation_z_rad = 0},
                  PuppetPoseJoint{.joint_id = "hand_r", .rotation_z_rad = 0},
              },
      },
  };
  return doc;
}

} // namespace

PuppetReferencePack make_historical_paper_figure_pack() {
  PuppetReferencePack pack;
  pack.asset_id = "historical-paper-figure-v1";
  pack.display_name = "Historical Paper Figure";
  pack.epoch_id = "victorian";
  pack.license = PuppetAssetLicense{};
  pack.rig = make_historical_figure_rig();
  pack.expressions = make_default_paper_face_library(pack.asset_id);
  pack.gestures = make_curated_gesture_library();
  pack.retarget = make_reference_human_retarget_map();
  return pack;
}

std::expected<void, diagnostics::Error>
validate_puppet_reference_pack(const PuppetReferencePack &pack) {
  if (pack.asset_id.empty() || pack.display_name.empty() ||
      pack.epoch_id.empty()) {
    return std::unexpected(make_err(
        "missing_field", "asset_id, display_name, and epoch_id are required"));
  }
  if (pack.license.spdx_id.empty() || pack.license.copyright_notice.empty()) {
    return std::unexpected(make_err(
        "missing_license", "Reference pack requires SPDX license metadata"));
  }
  if (auto ok = validate_puppet_rig(pack.rig); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = validate_puppet_expression_library(pack.expressions); !ok) {
    return std::unexpected(ok.error());
  }
  if (pack.expressions.expressions.size() < 3U) {
    return std::unexpected(make_err("missing_expressions",
                                    "Pack needs at least three expressions"));
  }
  if (pack.gestures.size() < 3U) {
    return std::unexpected(
        make_err("missing_gestures", "Pack needs a curated gesture set"));
  }
  for (const auto &clip : pack.gestures) {
    if (auto ok = validate_puppet_gesture_clip(clip); !ok) {
      return std::unexpected(ok.error());
    }
  }
  // Retarget must cover every role used by every gesture keyframe.
  for (const auto &clip : pack.gestures) {
    for (const auto &key : clip.keys) {
      for (const auto &[role, _] : key.role_poses) {
        if (!pack.retarget.contains(role) || pack.retarget.at(role).empty()) {
          return std::unexpected(
              make_err("missing_retarget",
                       "Gesture role missing from pack retarget map"));
        }
      }
    }
  }
  return {};
}

std::expected<PuppetReferencePoseSample, diagnostics::Error>
evaluate_puppet_reference_pose(const PuppetReferencePack &pack,
                               std::string_view pose_id, double gesture_t) {
  if (auto ok = validate_puppet_reference_pack(pack); !ok) {
    return std::unexpected(ok.error());
  }
  if (pose_id.empty()) {
    return std::unexpected(make_err("invalid_id", "Pose id is empty"));
  }

  PuppetReferencePoseSample sample;
  sample.label = std::string(pose_id);

  if (pose_id == "bind") {
    sample.kind = PuppetReferencePoseKind::bind;
    return sample;
  }

  for (const auto &expr : pack.expressions.expressions) {
    if (expr.id == pose_id) {
      auto pose = evaluate_named_puppet_expression(pack.expressions, pose_id);
      if (!pose) {
        return std::unexpected(pose.error());
      }
      sample.kind = PuppetReferencePoseKind::expression;
      sample.expression = std::move(*pose);
      sample.has_expression = true;
      sample.joint_poses = sample.expression.joint_poses;
      return sample;
    }
  }

  for (const auto &clip : pack.gestures) {
    if (clip.id == pose_id) {
      auto gest = sample_puppet_gesture(clip, pack.retarget, gesture_t);
      if (!gest) {
        return std::unexpected(gest.error());
      }
      sample.kind = PuppetReferencePoseKind::gesture;
      sample.joint_poses = std::move(gest->joint_poses);
      return sample;
    }
  }

  return std::unexpected(
      make_err("unknown_pose", "Pose id is not bind, expression, or gesture"));
}

std::string format_puppet_reference_pose_golden(
    std::string_view label, const PuppetReferencePack &pack,
    const PuppetReferencePoseSample &sample, const PuppetFkSkeleton &posed) {
  std::ostringstream os;
  os << "puppet_reference_pose " << label << '\n';
  os << "asset=" << pack.asset_id << " epoch=" << pack.epoch_id
     << " pose=" << sample.label << " kind=";
  switch (sample.kind) {
  case PuppetReferencePoseKind::bind:
    os << "bind";
    break;
  case PuppetReferencePoseKind::gesture:
    os << "gesture";
    break;
  case PuppetReferencePoseKind::expression:
    os << "expression";
    break;
  }
  os << " joints=" << posed.joints.size() << '\n';
  os << std::fixed << std::setprecision(6);
  for (const auto &joint : posed.joints) {
    const auto p = puppet_fk_world_position(joint);
    os << "j " << joint.id << " x=" << p.x << " y=" << p.y
       << " rz=" << joint.local.rotation_z_rad << '\n';
  }
  if (sample.has_expression) {
    for (const auto &part : sample.expression.parts) {
      os << "p " << part.part_id << " sx=" << part.scale_x
         << " sy=" << part.scale_y << " o=" << part.opacity << '\n';
    }
  }
  return os.str();
}

std::string
format_puppet_reference_pack_manifest(const PuppetReferencePack &pack) {
  std::ostringstream os;
  os << "puppet_reference_pack\n";
  os << "asset_id=" << pack.asset_id << '\n';
  os << "display_name=" << pack.display_name << '\n';
  os << "epoch_id=" << pack.epoch_id << '\n';
  os << "license_spdx=" << pack.license.spdx_id << '\n';
  os << "rig_id=" << pack.rig.id << " joints=" << pack.rig.joints.size()
     << " layers=" << pack.rig.layers.size() << '\n';
  os << "expressions=";
  for (std::size_t i = 0; i < pack.expressions.expressions.size(); ++i) {
    if (i != 0) {
      os << ',';
    }
    os << pack.expressions.expressions[i].id;
  }
  os << '\n';
  os << "gestures=";
  for (std::size_t i = 0; i < pack.gestures.size(); ++i) {
    if (i != 0) {
      os << ',';
    }
    os << pack.gestures[i].id;
  }
  os << '\n';
  return os.str();
}

} // namespace instantee::scene_ir
