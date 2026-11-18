#include <instantee/scene_ir/puppet_fk.hpp>
#include <instantee/scene_ir/puppet_gesture.hpp>
#include <instantee/scene_ir/puppet_rig.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
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

[[nodiscard]] const PuppetGestureClip *
find_clip(const std::vector<PuppetGestureClip> &clips, std::string_view id) {
  for (const auto &clip : clips) {
    if (clip.id == id) {
      return &clip;
    }
  }
  return nullptr;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: puppet_gesture_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const std::filesystem::path golden_dir =
      root / "contracts/fixtures/puppet-gesture/goldens";

  auto doc = make_reference_human_rig();
  expect(validate_puppet_rig(doc).has_value(), "reference human validates");
  auto bind = build_puppet_fk_skeleton(doc);
  expect(bind.has_value(), "bind skeleton");
  if (!bind) {
    return EXIT_FAILURE;
  }
  expect(evaluate_puppet_fk(*bind, doc).has_value(), "bind FK");

  const auto retarget = make_reference_human_retarget_map();
  auto library = make_curated_gesture_library();
  expect(library.size() == 5U, "five curated gestures");

  for (const char *id : {"raise-hand", "point", "grasp", "look-at", "nod"}) {
    const auto *clip = find_clip(library, id);
    expect(clip != nullptr, "clip present");
    if (!clip) {
      continue;
    }
    expect(validate_puppet_gesture_clip(*clip).has_value(), "clip validates");

    auto a = sample_puppet_gesture(*clip, retarget, 1.0);
    auto b = sample_puppet_gesture(*clip, retarget, 1.0);
    expect(a && b, "sample end");
    if (!a || !b) {
      continue;
    }
    expect(a->joint_poses.size() == b->joint_poses.size(),
           "deterministic size");
    for (const auto &[joint, pose] : a->joint_poses) {
      expect(nearly_equal(pose.rotation_z_rad,
                          b->joint_poses.at(joint).rotation_z_rad),
             "deterministic rotations");
    }

    auto posed = evaluate_puppet_gesture(doc, *bind, *a);
    expect(posed.has_value(), "evaluate gesture FK");
    if (!posed) {
      continue;
    }

    // Reproducibility: second evaluate matches world tip positions.
    auto posed2 = evaluate_puppet_gesture(doc, *bind, *b);
    expect(posed2.has_value(), "second evaluate");
    if (posed2) {
      for (std::size_t i = 0; i < posed->joints.size(); ++i) {
        expect(nearly_equal(puppet_fk_world_position(posed->joints[i]),
                            puppet_fk_world_position(posed2->joints[i])),
               "reproducible world positions");
      }
    }

    const std::string golden = format_puppet_gesture_golden(
        std::string(id) + "-t1", *clip, *posed, 1.0);
    const auto path = golden_dir / (std::string(id) + "-t1.txt");
    expect(write_if_missing(path, golden), "write golden");
    expect(read_file(path) == golden, "golden matches");
  }

  // raise-hand should lift the right hand above the shoulder bind Y.
  {
    const auto *clip = find_clip(library, "raise-hand");
    expect(clip != nullptr, "raise-hand exists");
    if (clip) {
      auto sample = sample_puppet_gesture(*clip, retarget, 1.0);
      auto posed = evaluate_puppet_gesture(doc, *bind, *sample);
      expect(posed.has_value(), "raise-hand pose");
      if (posed) {
        const auto hand = puppet_fk_world_position(
            posed->joints[posed->index_by_id.at("hand_r")]);
        const auto shoulder_bind = puppet_fk_world_position(
            bind->joints[bind->index_by_id.at("shoulder_r")]);
        expect(hand.y < shoulder_bind.y,
               "raised hand moves upward (-Y canvas)");
        expect(std::isfinite(hand.x) && std::isfinite(hand.y), "hand finite");
      }
    }
  }

  // Missing retarget mapping fails closed.
  {
    const auto *clip = find_clip(library, "nod");
    PuppetGestureRetargetMap incomplete{{"head", "head"}};
    // nod only uses head — should work. Use raise-hand for missing role.
    const auto *arm = find_clip(library, "raise-hand");
    expect(arm != nullptr, "arm clip");
    if (arm) {
      auto bad = sample_puppet_gesture(*arm, incomplete, 0.5);
      expect(!bad.has_value(), "missing retarget rejected");
      if (!bad) {
        expect(bad.error().code == "missing_retarget",
               "explicit retarget code");
      }
    }
    (void)clip;
  }

  // Unknown joint after bad retarget target fails at FK.
  {
    const auto *clip = find_clip(library, "nod");
    expect(clip != nullptr, "nod exists");
    if (clip) {
      PuppetGestureRetargetMap bad_map{{"head", "ghost"}};
      auto sample = sample_puppet_gesture(*clip, bad_map, 0.5);
      expect(sample.has_value(), "sample with ghost id still builds poses");
      if (sample) {
        auto posed = evaluate_puppet_gesture(doc, *bind, *sample);
        expect(!posed.has_value(), "unknown joint rejected by FK");
      }
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
