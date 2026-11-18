#include <instantee/scene_ir/puppet_fk.hpp>
#include <instantee/scene_ir/puppet_ik.hpp>
#include <instantee/scene_ir/puppet_rig.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__)
#error "scene-ir tests must remain free of Metal/Apple headers"
#endif

namespace {

using namespace instantee::scene_ir;
using instantee::core::nearly_equal;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] bool
finite_pose_map(const std::unordered_map<std::string, PuppetJointPose> &poses) {
  for (const auto &[id, pose] : poses) {
    if (!std::isfinite(pose.rotation_z_rad) ||
        !std::isfinite(pose.translate_delta.x) ||
        !std::isfinite(pose.translate_delta.y) ||
        !std::isfinite(pose.translate_delta.z)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] PuppetRigDocument make_arm_rig() {
  PuppetRigDocument doc;
  doc.schema_version = std::string{kPuppetRigSchemaVersion};
  doc.id = "ik-arm";
  doc.retarget.profile = std::string{kPuppetRigRetargetProfile};
  doc.retarget.space = std::string{kPuppetRigRetargetSpace};
  doc.joints = {
      PuppetJoint{.id = "root", .pivot = {0, 0}},
      PuppetJoint{.id = "shoulder", .parent_id = "root", .pivot = {40, 0}},
      PuppetJoint{.id = "elbow", .parent_id = "shoulder", .pivot = {80, 0}},
      PuppetJoint{.id = "hand", .parent_id = "elbow", .pivot = {110, 0}},
  };
  doc.constraints = {
      PuppetConstraint{.id = "elbow_hinge",
                       .type = PuppetConstraintType::hinge_limit,
                       .joint_id = "elbow",
                       .min_rad = -2.8,
                       .max_rad = 2.8},
  };
  return doc;
}

} // namespace

int main() {
  auto doc = make_arm_rig();
  auto skeleton = build_puppet_fk_skeleton(doc);
  expect(skeleton.has_value(), "build skeleton");
  if (!skeleton) {
    return EXIT_FAILURE;
  }

  // Reachable target: hand should approach (40, 70) style pose space.
  {
    const instantee::core::Vec3 target{40, 60, 0};
    auto a = solve_puppet_ik_2d(doc, *skeleton, "shoulder", "hand", target);
    expect(a.has_value(), "reachable solve");
    if (a) {
      expect(a->status == PuppetIkStatus::reached, "reachable status");
      expect(!a->used_reach_projection, "no projection");
      expect(a->tip_error_px <= 0.5, "reachable tolerance");
      expect(finite_pose_map(a->poses), "reachable finite");

      auto b = solve_puppet_ik_2d(doc, *skeleton, "shoulder", "hand", target);
      expect(b.has_value(), "second solve");
      if (b) {
        expect(a->status == b->status, "deterministic status");
        expect(a->iterations == b->iterations, "deterministic iterations");
        expect(nearly_equal(a->tip_error_px, b->tip_error_px),
               "deterministic error");
        expect(a->poses.size() == b->poses.size(), "deterministic pose count");
        for (const auto &[id, pose] : a->poses) {
          expect(
              nearly_equal(pose.rotation_z_rad, b->poses.at(id).rotation_z_rad),
              "deterministic rotations");
        }
      }

      PuppetFkSkeleton posed = *skeleton;
      expect(evaluate_puppet_fk(posed, doc, a->poses).has_value(),
             "apply IK poses");
      const auto tip =
          puppet_fk_world_position(posed.joints[posed.index_by_id.at("hand")]);
      const double err = std::hypot(tip.x - target.x, tip.y - target.y);
      expect(err <= 0.5 + 1e-6, "FK tip near target");
      expect(std::isfinite(tip.x) && std::isfinite(tip.y), "tip finite");
    }
  }

  // Unreachable target: fallback projects onto max reach.
  {
    const instantee::core::Vec3 far{1000, 0, 0};
    auto solved = solve_puppet_ik_2d(doc, *skeleton, "shoulder", "hand", far);
    expect(solved.has_value(), "unreachable solve");
    if (solved) {
      expect(solved->used_reach_projection, "projection used");
      expect(solved->status == PuppetIkStatus::fallback_unreachable ||
                 solved->status == PuppetIkStatus::reached,
             "unreachable fallback status");
      expect(finite_pose_map(solved->poses), "unreachable finite");
      // Max reach from shoulder: 40 + 30 = 70.
      const auto shoulder_bind =
          skeleton->joints[skeleton->index_by_id.at("shoulder")];
      const auto root_of_chain = puppet_fk_world_position(shoulder_bind);
      const double projected_dist =
          std::hypot(solved->solved_target.x - root_of_chain.x,
                     solved->solved_target.y - root_of_chain.y);
      expect(nearly_equal(projected_dist, 70.0, 1e-6, 1e-6),
             "projected onto max reach");
    }
  }

  // Two-segment minimum: shoulder→elbow→hand works; shoulder→hand alone is
  // three joints / two segments. Single segment must fail.
  {
    auto too_short = solve_puppet_ik_2d(doc, *skeleton, "elbow", "hand",
                                        instantee::core::Vec3{120, 10, 0});
    expect(!too_short.has_value(), "one-segment chain rejected");
  }

  // Non-finite target rejected.
  {
    auto bad = solve_puppet_ik_2d(
        doc, *skeleton, "shoulder", "hand",
        instantee::core::Vec3{std::numeric_limits<double>::quiet_NaN(), 0, 0});
    expect(!bad.has_value(), "NaN target rejected");
  }

  // Unknown tip rejected.
  {
    auto bad = solve_puppet_ik_2d(doc, *skeleton, "shoulder", "ghost",
                                  instantee::core::Vec3{50, 10, 0});
    expect(!bad.has_value(), "unknown tip rejected");
  }

  // Tight hinge: solver stays finite and respects the clamp even if the tip
  // cannot fully reach the target (iteration_cap is acceptable).
  {
    PuppetRigDocument tight = doc;
    tight.constraints = {
        PuppetConstraint{.id = "elbow_tight",
                         .type = PuppetConstraintType::hinge_limit,
                         .joint_id = "elbow",
                         .min_rad = -0.15,
                         .max_rad = 0.15},
    };
    auto tight_sk = build_puppet_fk_skeleton(tight);
    expect(tight_sk.has_value(), "tight skeleton");
    if (tight_sk) {
      const instantee::core::Vec3 target{40, 70, 0};
      auto solved =
          solve_puppet_ik_2d(tight, *tight_sk, "shoulder", "hand", target);
      expect(solved.has_value(), "tight hinge solve");
      if (solved) {
        expect(finite_pose_map(solved->poses), "tight hinge finite");
        if (solved->poses.contains("elbow")) {
          const double elbow = solved->poses.at("elbow").rotation_z_rad;
          expect(elbow >= -0.15 - 1e-9 && elbow <= 0.15 + 1e-9,
                 "elbow clamped by tight hinge");
        }
      }
    }
  }

  // Property-style sweep: many targets stay finite and deterministic.
  {
    std::vector<instantee::core::Vec3> samples;
    for (int i = -5; i <= 5; ++i) {
      for (int j = -5; j <= 5; ++j) {
        samples.push_back(instantee::core::Vec3{40.0 + i * 8.0, j * 8.0, 0.0});
      }
    }
    for (const auto &target : samples) {
      auto first =
          solve_puppet_ik_2d(doc, *skeleton, "shoulder", "hand", target);
      auto second =
          solve_puppet_ik_2d(doc, *skeleton, "shoulder", "hand", target);
      expect(first.has_value() && second.has_value(), "sweep solves");
      if (!first || !second) {
        continue;
      }
      expect(finite_pose_map(first->poses), "sweep finite");
      expect(first->iterations == second->iterations, "sweep deterministic");
      expect(nearly_equal(first->tip_error_px, second->tip_error_px),
             "sweep error match");
      expect(std::isfinite(first->tip_error_px), "sweep error finite");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
