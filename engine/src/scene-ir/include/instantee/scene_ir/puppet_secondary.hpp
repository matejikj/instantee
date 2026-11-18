#pragma once

#include <instantee/core_model/transform.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/scene_ir/puppet_expression.hpp>
#include <instantee/scene_ir/puppet_fk.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace instantee::scene_ir {

// Analytic secondary-motion rates/amplitudes. Periods are wall-clock seconds
// at the configured fps; evaluation is a pure function of absolute frame.
struct PuppetSecondaryMotionParams {
  double fps = 60.0;
  double blink_period_s = 3.2;
  double blink_duration_s = 0.12;
  double breath_period_s = 4.0;
  double breath_scale_amp = 0.025;
  double sway_period_s = 5.5;
  double sway_amp_rad = 0.035;
  double ear_period_s = 2.7;
  double ear_amp_px = 1.25;
  double cloth_period_s = 3.1;
  double cloth_amp_px = 1.75;
};

// Binding of secondary channels onto a character. Part ids for ears/cloth are
// additive drivers (not required to exist in the expression face library).
struct PuppetSecondaryMotionConfig {
  std::string node_id = "puppet";
  std::uint64_t seed = 1;
  std::string spine_joint_id = "spine";
  std::string head_joint_id = "head";
  std::string torso_part_id = "torso";
  std::string ear_l_part_id = "ear_l";
  std::string ear_r_part_id = "ear_r";
  std::string cloth_part_id = "cloth";
  PuppetSecondaryMotionParams params{};
};

struct PuppetSecondaryMotionPose {
  // Face channels driven procedurally (blink). Other channels stay 0.
  PuppetExpressionWeights channels{};
  std::unordered_map<std::string, PuppetJointPose> joint_poses;
  std::vector<PuppetPartDriver> parts;
};

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_puppet_secondary_motion_config(
    const PuppetSecondaryMotionConfig &config);

// Evaluate secondary motion at an absolute frame index. Independent of any
// prior FrameState (SAS §7.1). Non-finite frames fail closed.
[[nodiscard]] std::expected<PuppetSecondaryMotionPose, diagnostics::Error>
evaluate_puppet_secondary_motion(const PuppetSecondaryMotionConfig &config,
                                 std::int64_t frame);

// Layer secondary drivers onto an expression pose. Matching joint/part ids
// accumulate; blink takes max(expression, secondary). Unknown base parts are
// preserved; secondary-only parts are appended.
[[nodiscard]] std::expected<PuppetExpressionPose, diagnostics::Error>
compose_puppet_expression_with_secondary(
    const PuppetExpressionPose &expression,
    const PuppetSecondaryMotionPose &secondary);

[[nodiscard]] std::string format_puppet_secondary_fixture(
    std::string_view label, const PuppetSecondaryMotionConfig &config,
    const PuppetSecondaryMotionPose &pose, std::int64_t frame);

} // namespace instantee::scene_ir
