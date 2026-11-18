#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/scene_ir/puppet_expression.hpp>
#include <instantee/scene_ir/puppet_fk.hpp>
#include <instantee/scene_ir/puppet_gesture.hpp>
#include <instantee/scene_ir/puppet_rig.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace instantee::scene_ir {

// SPDX-style license metadata for curated Paper Cinema reference assets.
// Values are placeholders suitable for public fixtures (no personal identity).
struct PuppetAssetLicense {
  std::string spdx_id = "CC0-1.0";
  std::string copyright_notice =
      "Copyright-free dedication for Instantee reference fixtures.";
  std::string attribution_text =
      "Historical Paper Figure — Instantee reference puppet pack.";
};

// One painted paper character packaging a validated rig, face expressions, and
// retargetable gesture clips. Used as the Paper Cinema reference puppet.
struct PuppetReferencePack {
  std::string asset_id;
  std::string display_name;
  std::string epoch_id; // Style Bible epoch key (e.g. victorian).
  PuppetAssetLicense license{};
  PuppetRigDocument rig{};
  PuppetExpressionLibrary expressions{};
  std::vector<PuppetGestureClip> gestures{};
  PuppetGestureRetargetMap retarget{};
};

enum class PuppetReferencePoseKind : std::uint8_t {
  bind = 1,
  gesture = 2,
  expression = 3,
};

struct PuppetReferencePoseSample {
  PuppetReferencePoseKind kind = PuppetReferencePoseKind::bind;
  std::string label;
  std::unordered_map<std::string, PuppetJointPose> joint_poses;
  PuppetExpressionPose expression{};
  bool has_expression = false;
};

[[nodiscard]] PuppetReferencePack make_historical_paper_figure_pack();

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_puppet_reference_pack(const PuppetReferencePack &pack);

// Evaluate a named pose for goldens: "bind", gesture id, or expression id.
[[nodiscard]] std::expected<PuppetReferencePoseSample, diagnostics::Error>
evaluate_puppet_reference_pose(const PuppetReferencePack &pack,
                               std::string_view pose_id,
                               double gesture_t = 1.0);

[[nodiscard]] std::string format_puppet_reference_pose_golden(
    std::string_view label, const PuppetReferencePack &pack,
    const PuppetReferencePoseSample &sample, const PuppetFkSkeleton &posed);

[[nodiscard]] std::string
format_puppet_reference_pack_manifest(const PuppetReferencePack &pack);

} // namespace instantee::scene_ir
