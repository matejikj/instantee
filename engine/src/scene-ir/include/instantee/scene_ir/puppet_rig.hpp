#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::scene_ir {

// Versioned Paper Cinema puppet-rig payload referenced by opaque Scene IR Rig
// stubs (id / format_version / payload_ref). Schema lives under
// contracts/schemas/puppet-rig-v1.schema.json. Validation is CPU-only.
inline constexpr std::string_view kPuppetRigSchemaVersion = "1.0.0";
inline constexpr std::string_view kPuppetRigRetargetProfile = "paper_2d_v1";
inline constexpr std::string_view kPuppetRigRetargetSpace = "2d";

struct PuppetRigLimits {
  std::size_t max_joints = 64;
  std::size_t max_bones = 128;
  std::size_t max_layers = 32;
  std::size_t max_skin_bindings = 32;
  std::size_t max_influences = 4;
  std::size_t max_constraints = 64;
  std::size_t max_poses = 16;
  std::size_t max_string_bytes = 128;
  std::size_t max_bytes = 1U * 1024U * 1024U;
};

[[nodiscard]] constexpr PuppetRigLimits puppet_rig_limits_v1() noexcept {
  return {};
}

struct PuppetVec2 {
  double x = 0;
  double y = 0;
};

struct PuppetJoint {
  std::string id;
  std::optional<std::string> parent_id;
  PuppetVec2 pivot{};
};

struct PuppetBone {
  std::string id;
  std::string from_joint_id;
  std::string to_joint_id;
};

struct PuppetLayer {
  std::string id;
  std::string bind_joint_id;
};

struct PuppetSkinInfluence {
  std::string joint_id;
  double weight = 0;
};

struct PuppetSkinBinding {
  std::string layer_id;
  std::vector<PuppetSkinInfluence> influences;
};

enum class PuppetConstraintType : std::uint8_t { parent, hinge_limit };

struct PuppetConstraint {
  std::string id;
  PuppetConstraintType type = PuppetConstraintType::parent;
  std::string joint_id;
  std::optional<double> min_rad;
  std::optional<double> max_rad;
};

struct PuppetPoseJoint {
  std::string joint_id;
  double rotation_z_rad = 0;
  std::optional<PuppetVec2> translate;
};

struct PuppetReferencePose {
  std::string id;
  std::vector<PuppetPoseJoint> joints;
};

struct PuppetRetarget {
  std::string profile = std::string(kPuppetRigRetargetProfile);
  std::string space = std::string(kPuppetRigRetargetSpace);
  std::uint32_t max_joints = 64;
  std::uint32_t max_influences_per_vertex = 4;
};

// Value document for the puppet-rig interchange payload. No GPU handles.
struct PuppetRigDocument {
  std::string schema_version = std::string(kPuppetRigSchemaVersion);
  std::string id;
  PuppetRetarget retarget{};
  std::vector<PuppetJoint> joints;
  std::vector<PuppetBone> bones;
  std::vector<PuppetLayer> layers;
  std::vector<PuppetSkinBinding> skin_bindings;
  std::vector<PuppetConstraint> constraints;
  std::vector<PuppetReferencePose> reference_poses;
};

// Structural parse with unknown-field rejection. Does not attach GPU state.
[[nodiscard]] std::expected<PuppetRigDocument, diagnostics::Error>
parse_puppet_rig_json(std::string_view json_text,
                      const PuppetRigLimits &limits = puppet_rig_limits_v1());

// Semantic checks: unique ids, single-root DAG, bone/layer/skin/constraint/
// pose references, hinge ordering, skin weight sum ≈ 1, paper_2d_v1 only.
[[nodiscard]] std::expected<void, diagnostics::Error>
validate_puppet_rig(const PuppetRigDocument &document,
                    const PuppetRigLimits &limits = puppet_rig_limits_v1());

[[nodiscard]] std::expected<PuppetRigDocument, diagnostics::Error>
load_puppet_rig_json(std::string_view json_text,
                     const PuppetRigLimits &limits = puppet_rig_limits_v1());

} // namespace instantee::scene_ir
