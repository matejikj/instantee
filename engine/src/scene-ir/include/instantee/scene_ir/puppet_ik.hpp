#pragma once

#include <instantee/core_model/transform.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/scene_ir/puppet_fk.hpp>
#include <instantee/scene_ir/puppet_rig.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>

namespace instantee::scene_ir {

// Deterministic 2D CCD IK for paper puppet chains. No GPU, no RNG.
inline constexpr std::uint32_t kPuppetIkDefaultMaxIterations = 32;
inline constexpr double kPuppetIkDefaultTolerancePx = 0.5;

enum class PuppetIkStatus : std::uint8_t {
  // Tip within tolerance of the (possibly projected) target.
  reached = 1,
  // Target was outside max reach; solved toward the projected point on the
  // reach circle. This is the defined unreachable fallback.
  fallback_unreachable,
  // Iteration cap hit before tolerance; poses remain finite.
  iteration_cap
};

struct PuppetIkOptions {
  std::uint32_t max_iterations = kPuppetIkDefaultMaxIterations;
  double tolerance_px = kPuppetIkDefaultTolerancePx;
  bool apply_hinge_limits = true;
};

struct PuppetIkResult {
  PuppetIkStatus status = PuppetIkStatus::iteration_cap;
  std::uint32_t iterations = 0;
  double tip_error_px = 0;
  // True when the authored target was outside the chain's max reach and the
  // solver aimed at the projected fallback point instead.
  bool used_reach_projection = false;
  core::Vec3 solved_target{};
  std::unordered_map<std::string, PuppetJointPose> poses;
};

// Solve IK for a contiguous parent chain from chain_root_id to tip_id.
// Requires at least two bone segments (three joints). Chain root stays fixed
// in world; only local rotations along the chain are written into poses.
// Non-finite inputs, unknown ids, or broken chains fail closed.
[[nodiscard]] std::expected<PuppetIkResult, diagnostics::Error>
solve_puppet_ik_2d(const PuppetRigDocument &document,
                   const PuppetFkSkeleton &skeleton,
                   std::string_view chain_root_id, std::string_view tip_id,
                   core::Vec3 target, PuppetIkOptions options = {});

} // namespace instantee::scene_ir
