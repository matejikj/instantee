#include <instantee/scene_ir/puppet_fk.hpp>
#include <instantee/scene_ir/puppet_rig.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <unordered_map>

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

[[nodiscard]] PuppetRigDocument make_arm_rig() {
  PuppetRigDocument doc;
  doc.schema_version = std::string{kPuppetRigSchemaVersion};
  doc.id = "arm-chain";
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
                       .min_rad = -2.5,
                       .max_rad = 0.1},
  };
  return doc;
}

} // namespace

int main() {
  auto doc = make_arm_rig();
  expect(validate_puppet_rig(doc).has_value(), "arm rig validates");

  auto skeleton = build_puppet_fk_skeleton(doc);
  expect(skeleton.has_value(), "build skeleton");
  if (!skeleton) {
    std::cerr << skeleton.error().snapshot() << '\n';
    return EXIT_FAILURE;
  }
  expect(skeleton->joints.size() == 4U, "four joints");
  expect(skeleton->joints[0].id == "root", "root first");
  expect(skeleton->index_by_id.at("hand") > skeleton->index_by_id.at("elbow"),
         "hand after elbow");
  expect(skeleton->index_by_id.at("elbow") >
             skeleton->index_by_id.at("shoulder"),
         "elbow after shoulder");

  // Bind pose: world positions equal authored bind pivots.
  expect(evaluate_puppet_fk(*skeleton, doc).has_value(), "evaluate bind");
  for (const auto &joint : skeleton->joints) {
    const auto pos = puppet_fk_world_position(joint);
    const auto it =
        std::find_if(doc.joints.begin(), doc.joints.end(),
                     [&](const PuppetJoint &j) { return j.id == joint.id; });
    expect(it != doc.joints.end(), "joint present");
    expect(
        nearly_equal(pos, instantee::core::Vec3{it->pivot.x, it->pivot.y, 0}),
        "bind world matches pivot");
  }

  // Rotate shoulder by +90° (canvas: +X toward +Y). Children must follow.
  std::unordered_map<std::string, PuppetJointPose> poses;
  poses["shoulder"] = PuppetJointPose{.rotation_z_rad = std::numbers::pi / 2.0};
  expect(evaluate_puppet_fk(*skeleton, doc, poses).has_value(),
         "evaluate shoulder rotate");

  const auto &shoulder = skeleton->joints[skeleton->index_by_id.at("shoulder")];
  const auto &elbow = skeleton->joints[skeleton->index_by_id.at("elbow")];
  const auto &hand = skeleton->joints[skeleton->index_by_id.at("hand")];

  const auto shoulder_pos = puppet_fk_world_position(shoulder);
  const auto elbow_pos = puppet_fk_world_position(elbow);
  const auto hand_pos = puppet_fk_world_position(hand);

  expect(nearly_equal(shoulder_pos, instantee::core::Vec3{40, 0, 0}),
         "shoulder stays at bind pivot");
  // Rest elbow offset from shoulder is (+40,0); after +90° → (0,+40).
  expect(nearly_equal(elbow_pos, instantee::core::Vec3{40, 40, 0}),
         "elbow follows shoulder rotation");
  // Rest hand offset from elbow (+30,0) also rotates → (0,+30) in world after
  // the same parent rotation, so hand = elbow + (0,30) = (40,70).
  expect(nearly_equal(hand_pos, instantee::core::Vec3{40, 70, 0}),
         "hand follows through elbow");

  // Parent*local consistency already enforced inside evaluate; re-check hand.
  const auto hand_expected = instantee::core::combine_parent_local(
      elbow.world_matrix, hand.local_matrix);
  expect(nearly_equal(hand_expected, hand.world_matrix),
         "hand world equals elbow*local");

  // Unknown pose joint fails closed.
  poses["ghost"] = PuppetJointPose{};
  expect(!evaluate_puppet_fk(*skeleton, doc, poses).has_value(),
         "unknown pose joint rejected");
  poses.erase("ghost");

  // Hinge clamp: request elbow beyond max_rad.
  poses.clear();
  poses["elbow"] = PuppetJointPose{.rotation_z_rad = 1.0};
  expect(evaluate_puppet_fk(*skeleton, doc, poses).has_value(),
         "evaluate clamped elbow");
  expect(nearly_equal(skeleton->joints[skeleton->index_by_id.at("elbow")]
                          .local.rotation_z_rad,
                      0.1),
         "elbow hinge clamped to max");

  // Without hinge application, value stays unclamped.
  expect(
      evaluate_puppet_fk(*skeleton, doc, poses,
                         PuppetFkEvaluateOptions{.apply_hinge_limits = false})
          .has_value(),
      "evaluate unclamped");
  expect(nearly_equal(skeleton->joints[skeleton->index_by_id.at("elbow")]
                          .local.rotation_z_rad,
                      1.0),
         "elbow unclamped when disabled");

  // Non-finite pose rejected.
  poses.clear();
  poses["shoulder"] = PuppetJointPose{
      .rotation_z_rad = std::numeric_limits<double>::quiet_NaN()};
  expect(!evaluate_puppet_fk(*skeleton, doc, poses).has_value(),
         "NaN pose rejected");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
