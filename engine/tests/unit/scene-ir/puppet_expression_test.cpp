#include <instantee/scene_ir/puppet_expression.hpp>

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

[[nodiscard]] std::vector<std::string>
part_ids_of(const PuppetExpressionPose &pose) {
  std::vector<std::string> ids;
  ids.reserve(pose.parts.size());
  for (const auto &part : pose.parts) {
    ids.push_back(part.part_id);
  }
  return ids;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: puppet_expression_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const std::filesystem::path fixture_dir =
      root / "contracts/fixtures/puppet-expression";

  auto library = make_default_paper_face_library("paper-human");
  expect(validate_puppet_expression_library(library).has_value(),
         "default library validates");
  expect(library.expressions.size() == 3U, "three curated expressions");
  expect(library.part_ids.size() == 4U, "four stable face parts");

  auto neutral = evaluate_named_puppet_expression(library, "neutral");
  auto smile = evaluate_named_puppet_expression(library, "smile");
  auto surprise = evaluate_named_puppet_expression(library, "surprise");
  expect(neutral && smile && surprise, "three expressions evaluate");
  if (!neutral || !smile || !surprise) {
    return EXIT_FAILURE;
  }

  const auto ids_n = part_ids_of(*neutral);
  const auto ids_s = part_ids_of(*smile);
  const auto ids_u = part_ids_of(*surprise);
  expect(ids_n == library.part_ids, "neutral part ids stable");
  expect(ids_s == library.part_ids, "smile part ids stable");
  expect(ids_u == library.part_ids, "surprise part ids stable");
  expect(ids_n == ids_s && ids_s == ids_u,
         "part ids identical across expressions");

  // Smile opens mouth relative to neutral.
  double mouth_n = 1.0;
  double mouth_s = 1.0;
  double mouth_u = 1.0;
  for (const auto &part : neutral->parts) {
    if (part.part_id == kPuppetPartMouth) {
      mouth_n = part.scale_y;
    }
  }
  for (const auto &part : smile->parts) {
    if (part.part_id == kPuppetPartMouth) {
      mouth_s = part.scale_y;
    }
  }
  for (const auto &part : surprise->parts) {
    if (part.part_id == kPuppetPartMouth) {
      mouth_u = part.scale_y;
    }
  }
  expect(mouth_s > mouth_n, "smile mouth open > neutral");
  expect(mouth_u > mouth_s, "surprise mouth open > smile");

  expect(surprise->joint_poses.contains("head"), "surprise drives head joint");
  expect(surprise->joint_poses.at("head").rotation_z_rad > 0.0,
         "surprise head turn");

  // Blend preserves the same part id set.
  auto blend = evaluate_puppet_expression_blend(
      library, {{"smile", 0.5}, {"surprise", 0.5}});
  expect(blend.has_value(), "blend evaluates");
  if (blend) {
    expect(part_ids_of(*blend) == library.part_ids, "blend part ids stable");
  }

  // Unknown expression fails closed.
  expect(!evaluate_named_puppet_expression(library, "angry").has_value(),
         "unknown expression rejected");
  expect(
      !evaluate_puppet_expression_blend(library, {{"smile", -1.0}}).has_value(),
      "negative blend weight rejected");

  // Fixture dump for the three expressions.
  std::ostringstream fixture;
  fixture << format_puppet_expression_fixture("neutral", library, *neutral);
  fixture << format_puppet_expression_fixture("smile", library, *smile);
  fixture << format_puppet_expression_fixture("surprise", library, *surprise);
  const auto fixture_path = fixture_dir / "paper-human-three-expressions.txt";
  expect(write_if_missing(fixture_path, fixture.str()), "write fixture");
  const auto on_disk = read_file(fixture_path);
  expect(!on_disk.empty(), "fixture readable");
  expect(on_disk == fixture.str(), "fixture matches evaluated expressions");
  expect(on_disk.find("part eye_l") != std::string::npos, "fixture has eye_l");
  expect(on_disk.find("part mouth") != std::string::npos, "fixture has mouth");
  expect(on_disk.find("puppet_expression surprise") != std::string::npos,
         "fixture has surprise");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
