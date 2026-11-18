#include <instantee/scene_ir/puppet_fk.hpp>
#include <instantee/scene_ir/puppet_reference_pack.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__)
#error "scene-ir tests must remain free of Metal/Apple headers"
#endif

namespace {

using namespace instantee::scene_ir;

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

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: puppet_reference_pack_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const std::filesystem::path pack_dir =
      root / "contracts/fixtures/puppet-reference-pack";
  const std::filesystem::path golden_dir = pack_dir / "goldens";
  const std::filesystem::path license_path = pack_dir / "LICENSE.txt";

  auto pack = make_historical_paper_figure_pack();
  expect(validate_puppet_reference_pack(pack).has_value(), "pack validates");
  expect(pack.license.spdx_id == "CC0-1.0", "CC0 license");
  expect(pack.epoch_id == "victorian", "victorian epoch");
  expect(pack.expressions.expressions.size() >= 3U, "three expressions");
  expect(pack.gestures.size() >= 5U, "gesture set");
  expect(std::filesystem::exists(license_path), "license sidecar present");
  if (std::filesystem::exists(license_path)) {
    const auto lic = read_file(license_path);
    expect(lic.find("CC0-1.0") != std::string::npos, "license mentions CC0");
    expect(lic.find("SPDX") != std::string::npos, "license has SPDX");
  }

  auto bind = build_puppet_fk_skeleton(pack.rig);
  expect(bind.has_value(), "bind skeleton");
  if (!bind) {
    return EXIT_FAILURE;
  }

  // Manifest fixture.
  {
    const std::string body = format_puppet_reference_pack_manifest(pack);
    const auto path = pack_dir / "manifest.txt";
    expect(write_if_missing(path, body), "write manifest");
    expect(read_file(path) == body, "manifest matches");
  }

  struct PoseCase {
    const char *id;
    const char *file;
    double gesture_t;
  };
  const PoseCase cases[] = {
      {"bind", "bind.txt", 0.0},
      {"raise-hand", "raise-hand-t1.txt", 1.0},
      {"nod", "nod-t035.txt", 0.35},
      {"smile", "smile.txt", 0.0},
      {"surprise", "surprise.txt", 0.0},
  };

  for (const auto &pc : cases) {
    auto sample = evaluate_puppet_reference_pose(pack, pc.id, pc.gesture_t);
    expect(sample.has_value(), "pose sample");
    if (!sample) {
      continue;
    }
    auto posed = *bind;
    expect(evaluate_puppet_fk(posed, pack.rig, sample->joint_poses).has_value(),
           "fk evaluate");
    const std::string body =
        format_puppet_reference_pose_golden(pc.id, pack, *sample, posed);
    const auto path = golden_dir / pc.file;
    expect(write_if_missing(path, body), "write golden");
    expect(read_file(path) == body, "golden matches");

    if (sample->kind == PuppetReferencePoseKind::expression) {
      expect(sample->has_expression, "expression drivers");
      expect(sample->expression.parts.size() ==
                 pack.expressions.part_ids.size(),
             "stable face part count");
    }
    if (std::string_view(pc.id) == "raise-hand") {
      const auto hand = puppet_fk_world_position(
          posed.joints[posed.index_by_id.at("hand_r")]);
      const auto shoulder = puppet_fk_world_position(
          bind->joints[bind->index_by_id.at("shoulder_r")]);
      expect(hand.y < shoulder.y, "raised hand above shoulder");
    }
  }

  // Unknown pose fails closed.
  expect(!evaluate_puppet_reference_pose(pack, "missing").has_value(),
         "unknown pose rejected");

  // Broken retarget fails validation.
  {
    auto bad = pack;
    bad.retarget.erase("shoulder_r");
    expect(!validate_puppet_reference_pack(bad).has_value(),
           "incomplete retarget rejected");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
