#include <instantee/scene_ir/puppet_fk.hpp>
#include <instantee/scene_ir/puppet_rig.hpp>
#include <instantee/scene_ir/puppet_skin.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
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

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

[[nodiscard]] bool write_if_missing(const std::filesystem::path &path,
                                    std::string_view body) {
  if (std::filesystem::exists(path)) {
    return true;
  }
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out << body;
  return static_cast<bool>(out);
}

[[nodiscard]] PuppetRigDocument make_arm_skin_rig() {
  PuppetRigDocument doc;
  doc.schema_version = std::string{kPuppetRigSchemaVersion};
  doc.id = "skin-arm";
  doc.retarget.profile = std::string{kPuppetRigRetargetProfile};
  doc.retarget.space = std::string{kPuppetRigRetargetSpace};
  doc.joints = {
      PuppetJoint{.id = "root", .pivot = {0, 0}},
      PuppetJoint{.id = "shoulder", .parent_id = "root", .pivot = {40, 0}},
      PuppetJoint{.id = "elbow", .parent_id = "shoulder", .pivot = {80, 0}},
      PuppetJoint{.id = "hand", .parent_id = "elbow", .pivot = {110, 0}},
  };
  doc.layers = {
      PuppetLayer{.id = "forearm", .bind_joint_id = "elbow"},
      PuppetLayer{.id = "blend_arm", .bind_joint_id = "shoulder"},
  };
  doc.skin_bindings = {
      PuppetSkinBinding{
          .layer_id = "forearm",
          .influences = {PuppetSkinInfluence{.joint_id = "elbow",
                                             .weight = 1.0}},
      },
      PuppetSkinBinding{
          .layer_id = "blend_arm",
          .influences =
              {
                  PuppetSkinInfluence{.joint_id = "shoulder", .weight = 0.5},
                  PuppetSkinInfluence{.joint_id = "elbow", .weight = 0.5},
              },
      },
  };
  return doc;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: puppet_skin_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const std::filesystem::path golden_dir =
      root / "contracts/fixtures/puppet-skin/goldens";

  auto doc = make_arm_skin_rig();
  auto bind = build_puppet_fk_skeleton(doc);
  expect(bind.has_value(), "bind skeleton");
  if (!bind) {
    return EXIT_FAILURE;
  }
  expect(evaluate_puppet_fk(*bind, doc).has_value(), "evaluate bind");

  auto forearm = make_puppet_skin_layer_quad(doc, *bind, "forearm", 10.0, 4.0);
  expect(forearm.has_value(), "forearm quad");
  if (!forearm) {
    std::cerr << forearm.error().snapshot() << '\n';
    return EXIT_FAILURE;
  }
  expect(forearm->vertices.size() == 4U, "four verts");
  expect(forearm->vertices[0].influence_count == 1U, "single influence");

  // Identity pose: deformed == bind.
  {
    auto mats = compute_puppet_skin_matrices(*bind, *bind);
    expect(mats.has_value(), "identity skin mats");
    auto def = deform_puppet_skin_part(*forearm, *mats);
    expect(def.has_value(), "identity deform");
    if (def) {
      for (std::size_t i = 0; i < def->size(); ++i) {
        expect(nearly_equal((*def)[i], forearm->vertices[i].bind_position),
               "identity preserves bind");
      }
    }
  }

  // Shoulder +90°: elbow moves to (40,40); forearm verts follow elbow rigidly.
  PuppetFkSkeleton posed = *bind;
  std::unordered_map<std::string, PuppetJointPose> poses;
  poses["shoulder"] = PuppetJointPose{.rotation_z_rad = std::numbers::pi / 2.0};
  expect(evaluate_puppet_fk(posed, doc, poses).has_value(), "pose FK");
  auto mats = compute_puppet_skin_matrices(*bind, posed);
  expect(mats.has_value(), "posed skin mats");
  auto deformed = deform_puppet_skin_part(*forearm, *mats);
  expect(deformed.has_value(), "posed deform");
  if (!deformed) {
    return EXIT_FAILURE;
  }

  const auto elbow_bind =
      puppet_fk_world_position(bind->joints[bind->index_by_id.at("elbow")]);
  const auto elbow_posed =
      puppet_fk_world_position(posed.joints[posed.index_by_id.at("elbow")]);
  expect(nearly_equal(elbow_posed, instantee::core::Vec3{40, 40, 0}),
         "elbow posed reference");

  for (std::size_t i = 0; i < deformed->size(); ++i) {
    const auto &b = forearm->vertices[i].bind_position;
    // Rigid around elbow: rotate offset by +90° then add posed elbow.
    const double ox = b.x - elbow_bind.x;
    const double oy = b.y - elbow_bind.y;
    // +90°: (x,y) -> (-y, x) in canvas (+X right, +Y down).
    const instantee::core::Vec3 expected{elbow_posed.x - oy, elbow_posed.y + ox,
                                         0.0};
    expect(nearly_equal((*deformed)[i], expected, 1e-6, 1e-6),
           "rigid forearm golden math");
  }

  const std::string golden_body =
      format_puppet_skin_golden("forearm-shoulder-90", *forearm, *deformed);
  const auto golden_path = golden_dir / "forearm-shoulder-90.txt";
  expect(write_if_missing(golden_path, golden_body), "write golden if missing");
  const auto on_disk = read_file(golden_path);
  expect(!on_disk.empty(), "golden readable");
  expect(on_disk == golden_body, "golden matches reference deformation");

  // Blend layer still finite and deterministic.
  {
    auto blend = make_puppet_skin_layer_quad(doc, *bind, "blend_arm", 8.0, 3.0);
    expect(blend.has_value(), "blend quad");
    if (blend) {
      auto d1 = deform_puppet_skin_part(*blend, *mats);
      auto d2 = deform_puppet_skin_part(*blend, *mats);
      expect(d1.has_value() && d2.has_value(), "blend deform");
      if (d1 && d2) {
        expect(d1->size() == d2->size(), "blend size");
        for (std::size_t i = 0; i < d1->size(); ++i) {
          expect(nearly_equal((*d1)[i], (*d2)[i]), "blend deterministic");
          expect(std::isfinite((*d1)[i].x) && std::isfinite((*d1)[i].y),
                 "blend finite");
        }
      }
    }
  }

  // Unsupported: layer without skin_bindings → explicit error.
  {
    PuppetRigDocument bad = doc;
    bad.layers.push_back(PuppetLayer{.id = "orphan", .bind_joint_id = "hand"});
    auto orphan = make_puppet_skin_layer_quad(bad, *bind, "orphan", 1.0, 1.0);
    expect(!orphan.has_value(), "missing binding rejected");
    if (!orphan) {
      expect(orphan.error().code == "missing_skin_binding",
             "explicit missing binding code");
    }
  }

  // Unsupported retarget profile → explicit error (no natural fallback).
  {
    PuppetRigDocument bad = doc;
    bad.retarget.profile = "full_3d_mesh";
    // Bypass validate by mutating after build; skin entry checks profile.
    auto part = make_puppet_skin_layer_quad(bad, *bind, "forearm", 1.0, 1.0);
    expect(!part.has_value(), "unsupported retarget rejected");
    if (!part) {
      expect(part.error().code == "unsupported_rig",
             "explicit unsupported code");
    }
  }

  // Too many influences on a hand-authored vertex → deform fails closed.
  {
    PuppetSkinPart over = *forearm;
    over.vertices[0].influence_count = 5;
    auto bad = deform_puppet_skin_part(over, *mats);
    expect(!bad.has_value(), "too many influences rejected");
    if (!bad) {
      expect(bad.error().code == "unsupported_influences",
             "explicit influence budget code");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
