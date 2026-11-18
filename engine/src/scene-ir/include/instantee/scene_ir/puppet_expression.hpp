#pragma once

#include <instantee/core_model/transform.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/scene_ir/puppet_fk.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace instantee::scene_ir {

// Animatable face channels for paper puppets. Values are clamped to [0, 1].
enum class PuppetExpressionChannel : std::uint8_t {
  blink = 1,
  mouth_open = 2,
  head_turn = 3,
  eye_widen = 4
};

inline constexpr std::string_view kPuppetPartEyeL = "eye_l";
inline constexpr std::string_view kPuppetPartEyeR = "eye_r";
inline constexpr std::string_view kPuppetPartMouth = "mouth";
inline constexpr std::string_view kPuppetPartHead = "head";

inline constexpr double kPuppetHeadTurnMaxRad = 0.45;

struct PuppetExpressionWeights {
  double blink = 0;
  double mouth_open = 0;
  double head_turn = 0; // 0 = center, 1 = full left turn in +rotation_z
  double eye_widen = 0;
};

// One curated named expression. Part ids are implied by the character face
// set and must remain stable across all expressions of that character.
struct PuppetNamedExpression {
  std::string id;
  PuppetExpressionWeights weights{};
};

struct PuppetExpressionLibrary {
  std::string character_id;
  std::string head_joint_id = "head";
  std::vector<std::string> part_ids;
  std::vector<PuppetNamedExpression> expressions;
};

// Evaluated drivers applied on top of bind/FK. Part ids match library.part_ids.
struct PuppetPartDriver {
  std::string part_id;
  double scale_x = 1;
  double scale_y = 1;
  double opacity = 1;
  core::Vec3 translate_delta{};
};

struct PuppetExpressionPose {
  std::unordered_map<std::string, PuppetJointPose> joint_poses;
  std::vector<PuppetPartDriver> parts;
};

[[nodiscard]] PuppetExpressionLibrary
make_default_paper_face_library(std::string_view character_id);

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_puppet_expression_library(const PuppetExpressionLibrary &library);

// Blend named expression weights (non-negative; renormalized if sum > 0).
// Unknown expression ids fail closed. Resulting part ids equal library.part_ids
// in order for every successful evaluation.
[[nodiscard]] std::expected<PuppetExpressionPose, diagnostics::Error>
evaluate_puppet_expression_blend(
    const PuppetExpressionLibrary &library,
    const std::unordered_map<std::string, double> &expression_weights);

[[nodiscard]] std::expected<PuppetExpressionPose, diagnostics::Error>
evaluate_named_puppet_expression(const PuppetExpressionLibrary &library,
                                 std::string_view expression_id);

[[nodiscard]] std::string
format_puppet_expression_fixture(std::string_view label,
                                 const PuppetExpressionLibrary &library,
                                 const PuppetExpressionPose &pose);

} // namespace instantee::scene_ir
