#include <instantee/scene_ir/puppet_expression.hpp>
#include <instantee/scene_ir/puppet_secondary.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

[[nodiscard]] bool poses_equal(const PuppetSecondaryMotionPose &a,
                               const PuppetSecondaryMotionPose &b) {
  if (!nearly_equal(a.channels.blink, b.channels.blink) ||
      a.joint_poses.size() != b.joint_poses.size() ||
      a.parts.size() != b.parts.size()) {
    return false;
  }
  for (const auto &[id, pose] : a.joint_poses) {
    const auto it = b.joint_poses.find(id);
    if (it == b.joint_poses.end()) {
      return false;
    }
    if (!nearly_equal(pose.rotation_z_rad, it->second.rotation_z_rad) ||
        !nearly_equal(pose.translate_delta, it->second.translate_delta)) {
      return false;
    }
  }
  for (std::size_t i = 0; i < a.parts.size(); ++i) {
    const auto &pa = a.parts[i];
    const auto &pb = b.parts[i];
    if (pa.part_id != pb.part_id || !nearly_equal(pa.scale_x, pb.scale_x) ||
        !nearly_equal(pa.scale_y, pb.scale_y) ||
        !nearly_equal(pa.opacity, pb.opacity) ||
        !nearly_equal(pa.translate_delta, pb.translate_delta)) {
      return false;
    }
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: puppet_secondary_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const std::filesystem::path fixture_dir =
      root / "contracts/fixtures/puppet-secondary";

  PuppetSecondaryMotionConfig config;
  config.node_id = "paper-human";
  config.seed = 0xC0FFEEULL;
  expect(validate_puppet_secondary_motion_config(config).has_value(),
         "default config validates");

  // Random-access: scrambled order matches sequential evaluation.
  const std::int64_t frames[] = {0, 17, 60, 181, 240, 503, 720};
  std::unordered_map<std::int64_t, PuppetSecondaryMotionPose> by_frame;
  for (std::int64_t f : frames) {
    auto pose = evaluate_puppet_secondary_motion(config, f);
    expect(pose.has_value(), "evaluate frame");
    if (pose) {
      by_frame.emplace(f, *pose);
    }
  }
  // Re-evaluate in reverse and compare.
  for (std::size_t i = sizeof(frames) / sizeof(frames[0]); i > 0; --i) {
    const std::int64_t f = frames[i - 1];
    auto again = evaluate_puppet_secondary_motion(config, f);
    expect(again.has_value(), "reevaluate frame");
    if (again) {
      expect(poses_equal(by_frame.at(f), *again),
             "random-access pose identical");
    }
  }

  // Adding an unrelated node must not change this puppet's channels.
  {
    auto baseline = evaluate_puppet_secondary_motion(config, 181);
    PuppetSecondaryMotionConfig other = config;
    other.node_id = "unrelated-prop";
    auto distractor = evaluate_puppet_secondary_motion(other, 181);
    expect(baseline && distractor, "independent nodes evaluate");
    auto again = evaluate_puppet_secondary_motion(config, 181);
    expect(again && poses_equal(*baseline, *again),
           "unrelated node does not perturb pose");
    // Phase offsets are hashed by node_id, so sway/cloth diverge.
    if (baseline && distractor) {
      expect(!nearly_equal(baseline->joint_poses.at("spine").rotation_z_rad,
                           distractor->joint_poses.at("spine").rotation_z_rad),
             "independent node sway diverges");
      expect(!nearly_equal(baseline->parts.back().translate_delta,
                           distractor->parts.back().translate_delta),
             "independent node cloth diverges");
    }
  }

  // Negative frame / invalid config fail closed.
  expect(!evaluate_puppet_secondary_motion(config, -1).has_value(),
         "negative frame rejected");
  {
    PuppetSecondaryMotionConfig bad = config;
    bad.params.blink_duration_s = bad.params.blink_period_s;
    expect(!validate_puppet_secondary_motion_config(bad).has_value(),
           "blink duration >= period rejected");
  }

  // Composition with expression keeps stable face part ids and applies blink.
  {
    auto library = make_default_paper_face_library("paper-human");
    auto expression = evaluate_named_puppet_expression(library, "smile");
    auto secondary = evaluate_puppet_secondary_motion(config, 60);
    expect(expression && secondary, "compose inputs");
    if (expression && secondary) {
      // Force a strong blink in a synthetic secondary overlay.
      secondary->channels.blink = 1.0;
      auto composed =
          compose_puppet_expression_with_secondary(*expression, *secondary);
      expect(composed.has_value(), "compose ok");
      if (composed) {
        expect(composed->parts.size() >= library.part_ids.size(),
               "face parts preserved/extended");
        std::size_t matched = 0;
        for (const auto &id : library.part_ids) {
          for (const auto &part : composed->parts) {
            if (part.part_id == id) {
              ++matched;
              break;
            }
          }
        }
        expect(matched == library.part_ids.size(), "stable face part ids");
        for (const auto &part : composed->parts) {
          if (part.part_id == kPuppetPartEyeL ||
              part.part_id == kPuppetPartEyeR) {
            expect(part.scale_y < 0.2, "blink closes eyes");
          }
        }
        expect(composed->joint_poses.contains("spine"), "sway on spine");
        expect(composed->parts.size() > library.part_ids.size(),
               "ear/cloth/torso appended");
      }
    }
  }

  // Lock fixtures at a few absolute frames.
  const std::int64_t golden_frames[] = {0, 60, 181};
  for (std::int64_t f : golden_frames) {
    auto pose = evaluate_puppet_secondary_motion(config, f);
    expect(pose.has_value(), "golden evaluate");
    if (!pose) {
      continue;
    }
    const std::string body = format_puppet_secondary_fixture(
        "f" + std::to_string(f), config, *pose, f);
    const auto path =
        fixture_dir / ("paper-human-f" + std::to_string(f) + ".txt");
    expect(write_if_missing(path, body), "write fixture");
    expect(read_file(path) == body, "fixture matches");
  }

  // Breath/sway/ear/cloth channels are non-trivial over time (not frozen).
  {
    auto a = evaluate_puppet_secondary_motion(config, 10);
    auto b = evaluate_puppet_secondary_motion(config, 100);
    expect(a && b, "motion samples");
    if (a && b) {
      expect(!nearly_equal(a->joint_poses.at("spine").rotation_z_rad,
                           b->joint_poses.at("spine").rotation_z_rad),
             "sway evolves");
      expect(!nearly_equal(a->parts[0].scale_y, b->parts[0].scale_y),
             "breath evolves");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
