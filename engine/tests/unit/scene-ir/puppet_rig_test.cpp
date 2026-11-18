#include <instantee/scene_ir/puppet_rig.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__)
#error "scene-ir tests must remain free of Metal/Apple headers"
#endif

namespace {

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

[[nodiscard]] std::vector<std::filesystem::path>
list_json(const std::filesystem::path &dir) {
  std::vector<std::filesystem::path> out;
  if (!std::filesystem::exists(dir)) {
    return out;
  }
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      out.push_back(entry.path());
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: puppet_rig_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const std::filesystem::path fixtures = root / "contracts/fixtures/puppet-rig";

  expect(std::string{instantee::scene_ir::kPuppetRigSchemaVersion} == "1.0.0",
         "schema version");
  expect(std::string{instantee::scene_ir::kPuppetRigRetargetProfile} ==
             "paper_2d_v1",
         "retarget profile");

  const auto positive = list_json(fixtures / "positive");
  expect(!positive.empty(), "positive fixtures present");
  for (const auto &path : positive) {
    const auto text = read_file(path);
    expect(!text.empty(), "readable positive fixture");
    auto loaded = instantee::scene_ir::load_puppet_rig_json(text);
    if (!loaded) {
      std::cerr << "FAIL: positive rejected: " << path.filename()
                << " :: " << loaded.error().snapshot() << '\n';
      ++failures;
      continue;
    }
    expect(loaded->id == "paper-human-basic" || !loaded->id.empty(),
           "positive rig id");
    expect(loaded->joints.size() >= 1U, "positive has joints");
    expect(loaded->retarget.profile == "paper_2d_v1", "positive retarget");
  }

  for (const auto &path : list_json(fixtures / "negative")) {
    const auto text = read_file(path);
    expect(!text.empty(), "readable negative fixture");
    auto loaded = instantee::scene_ir::load_puppet_rig_json(text);
    expect(!loaded.has_value(), "schema-negative rejected without GPU");
    if (!loaded) {
      expect(loaded.error().phase == "puppet_rig", "negative phase");
      expect(loaded.error().kind ==
                 instantee::diagnostics::ErrorKind::invalid_input,
             "negative kind");
    }
  }

  for (const auto &path : list_json(fixtures / "semantic-negative")) {
    const auto text = read_file(path);
    expect(!text.empty(), "readable semantic-negative fixture");
    // Budgets may fail during parse; graph/skin rules fail during validate.
    // Either path must reject without touching GPU.
    auto loaded = instantee::scene_ir::load_puppet_rig_json(text);
    expect(!loaded.has_value(), "semantic-negative rejected without GPU");
    if (!loaded) {
      expect(loaded.error().phase == "puppet_rig", "semantic phase");
    }
  }

  // Explicit hinge inversion after a successful parse of a crafted document.
  {
    instantee::scene_ir::PuppetRigDocument doc;
    doc.id = "manual";
    doc.joints.push_back(
        instantee::scene_ir::PuppetJoint{.id = "root", .pivot = {0, 0}});
    doc.joints.push_back(instantee::scene_ir::PuppetJoint{
        .id = "elbow", .parent_id = "root", .pivot = {10, 0}});
    doc.constraints.push_back(instantee::scene_ir::PuppetConstraint{
        .id = "bad",
        .type = instantee::scene_ir::PuppetConstraintType::hinge_limit,
        .joint_id = "elbow",
        .min_rad = 1.0,
        .max_rad = -1.0});
    auto ok = instantee::scene_ir::validate_puppet_rig(doc);
    expect(!ok.has_value(), "inverted hinge rejected");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
