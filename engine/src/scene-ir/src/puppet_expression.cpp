#include <instantee/scene_ir/puppet_expression.hpp>

#include <cmath>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace instantee::scene_ir {
namespace {

diagnostics::Error make_err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "puppet_expression", message};
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

[[nodiscard]] PuppetExpressionWeights
clamp_weights(const PuppetExpressionWeights &in) noexcept {
  return PuppetExpressionWeights{
      .blink = clamp01(in.blink),
      .mouth_open = clamp01(in.mouth_open),
      .head_turn = clamp01(in.head_turn),
      .eye_widen = clamp01(in.eye_widen),
  };
}

[[nodiscard]] PuppetExpressionPose
drivers_from_weights(const PuppetExpressionLibrary &library,
                     const PuppetExpressionWeights &raw) {
  const auto w = clamp_weights(raw);
  PuppetExpressionPose pose;
  pose.joint_poses.emplace(
      library.head_joint_id,
      PuppetJointPose{.rotation_z_rad = w.head_turn * kPuppetHeadTurnMaxRad});

  pose.parts.reserve(library.part_ids.size());
  for (const auto &part_id : library.part_ids) {
    PuppetPartDriver driver{.part_id = part_id};
    if (part_id == kPuppetPartEyeL || part_id == kPuppetPartEyeR) {
      // Blink closes lids; eye_widen opens slightly. Blink dominates.
      const double open =
          std::max(0.0, 1.0 - 0.92 * w.blink + 0.18 * w.eye_widen);
      driver.scale_y = open;
      driver.opacity = std::min(1.0, std::max(0.15, open));
    } else if (part_id == kPuppetPartMouth) {
      driver.scale_y = 1.0 + 0.75 * w.mouth_open;
      driver.translate_delta.y = 2.0 * w.mouth_open;
    } else if (part_id == kPuppetPartHead) {
      // Head paper layer follows the joint; keep uniform scale.
      driver.scale_x = 1.0;
      driver.scale_y = 1.0;
    }
    pose.parts.push_back(std::move(driver));
  }
  return pose;
}

} // namespace

PuppetExpressionLibrary
make_default_paper_face_library(std::string_view character_id) {
  PuppetExpressionLibrary library;
  library.character_id = std::string(character_id);
  library.head_joint_id = "head";
  library.part_ids = {
      std::string(kPuppetPartEyeL),
      std::string(kPuppetPartEyeR),
      std::string(kPuppetPartMouth),
      std::string(kPuppetPartHead),
  };
  library.expressions = {
      PuppetNamedExpression{.id = "neutral", .weights = {}},
      PuppetNamedExpression{.id = "smile",
                            .weights = {.blink = 0.05,
                                        .mouth_open = 0.35,
                                        .head_turn = 0.0,
                                        .eye_widen = 0.1}},
      PuppetNamedExpression{.id = "surprise",
                            .weights = {.blink = 0.0,
                                        .mouth_open = 0.85,
                                        .head_turn = 0.2,
                                        .eye_widen = 0.7}},
  };
  return library;
}

std::expected<void, diagnostics::Error>
validate_puppet_expression_library(const PuppetExpressionLibrary &library) {
  if (library.character_id.empty() || library.head_joint_id.empty()) {
    return std::unexpected(make_err(
        "missing_field", "character_id and head_joint_id are required"));
  }
  if (library.part_ids.size() < 3U) {
    return std::unexpected(
        make_err("invalid_parts",
                 "Expression library needs at least three stable parts"));
  }
  if (library.expressions.size() < 3U) {
    return std::unexpected(
        make_err("invalid_expressions",
                 "Expression library needs at least three named expressions"));
  }

  std::unordered_map<std::string, std::size_t> part_seen;
  for (const auto &part : library.part_ids) {
    if (part.empty() || part.find('/') != std::string::npos) {
      return std::unexpected(make_err("invalid_id", "Invalid part id"));
    }
    if (!part_seen.emplace(part, 1).second) {
      return std::unexpected(make_err("duplicate_id", "Duplicate part id"));
    }
  }
  for (std::string_view required :
       {kPuppetPartEyeL, kPuppetPartEyeR, kPuppetPartMouth}) {
    if (!part_seen.contains(std::string(required))) {
      return std::unexpected(make_err(
          "missing_part", "Required face part id is missing from the library"));
    }
  }

  std::unordered_map<std::string, std::size_t> expr_seen;
  for (const auto &expression : library.expressions) {
    if (expression.id.empty()) {
      return std::unexpected(make_err("invalid_id", "Empty expression id"));
    }
    if (!expr_seen.emplace(expression.id, 1).second) {
      return std::unexpected(
          make_err("duplicate_id", "Duplicate expression id"));
    }
    const auto w = expression.weights;
    if (!std::isfinite(w.blink) || !std::isfinite(w.mouth_open) ||
        !std::isfinite(w.head_turn) || !std::isfinite(w.eye_widen)) {
      return std::unexpected(
          make_err("non_finite", "Expression weights must be finite"));
    }
  }
  return {};
}

std::expected<PuppetExpressionPose, diagnostics::Error>
evaluate_puppet_expression_blend(
    const PuppetExpressionLibrary &library,
    const std::unordered_map<std::string, double> &expression_weights) {
  if (auto ok = validate_puppet_expression_library(library); !ok) {
    return std::unexpected(ok.error());
  }

  std::unordered_map<std::string, const PuppetNamedExpression *> by_id;
  by_id.reserve(library.expressions.size());
  for (const auto &expression : library.expressions) {
    by_id.emplace(expression.id, &expression);
  }

  double sum = 0.0;
  PuppetExpressionWeights accum{};
  for (const auto &[id, weight] : expression_weights) {
    if (!std::isfinite(weight) || weight < 0.0) {
      return std::unexpected(
          make_err("invalid_weight",
                   "Expression blend weights must be finite and >= 0"));
    }
    if (weight == 0.0) {
      continue;
    }
    const auto it = by_id.find(id);
    if (it == by_id.end()) {
      return std::unexpected(
          make_err("unknown_expression", "Unknown expression id in blend"));
    }
    sum += weight;
    const auto &src = it->second->weights;
    accum.blink += src.blink * weight;
    accum.mouth_open += src.mouth_open * weight;
    accum.head_turn += src.head_turn * weight;
    accum.eye_widen += src.eye_widen * weight;
  }

  PuppetExpressionWeights blended{};
  if (sum > 0.0) {
    blended.blink = accum.blink / sum;
    blended.mouth_open = accum.mouth_open / sum;
    blended.head_turn = accum.head_turn / sum;
    blended.eye_widen = accum.eye_widen / sum;
  }

  auto pose = drivers_from_weights(library, blended);
  if (pose.parts.size() != library.part_ids.size()) {
    return std::unexpected(
        make_err("unstable_parts", "Part driver count drifted from library"));
  }
  for (std::size_t i = 0; i < pose.parts.size(); ++i) {
    if (pose.parts[i].part_id != library.part_ids[i]) {
      return std::unexpected(make_err(
          "unstable_parts", "Part ids are not stable across evaluate"));
    }
    if (!std::isfinite(pose.parts[i].scale_x) ||
        !std::isfinite(pose.parts[i].scale_y) ||
        !std::isfinite(pose.parts[i].opacity)) {
      return std::unexpected(
          make_err("non_finite", "Part drivers must be finite"));
    }
  }
  return pose;
}

std::expected<PuppetExpressionPose, diagnostics::Error>
evaluate_named_puppet_expression(const PuppetExpressionLibrary &library,
                                 std::string_view expression_id) {
  return evaluate_puppet_expression_blend(library,
                                          {{std::string(expression_id), 1.0}});
}

std::string
format_puppet_expression_fixture(std::string_view label,
                                 const PuppetExpressionLibrary &library,
                                 const PuppetExpressionPose &pose) {
  std::ostringstream os;
  os << "puppet_expression " << label << '\n';
  os << "character=" << library.character_id << " parts=" << pose.parts.size()
     << '\n';
  for (const auto &part : pose.parts) {
    os << "part " << part.part_id << " sx=" << part.scale_x
       << " sy=" << part.scale_y << " op=" << part.opacity << '\n';
  }
  if (const auto it = pose.joint_poses.find(library.head_joint_id);
      it != pose.joint_poses.end()) {
    os << "joint " << library.head_joint_id
       << " rz=" << it->second.rotation_z_rad << '\n';
  }
  return os.str();
}

} // namespace instantee::scene_ir
