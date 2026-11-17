#include <instantee/scene_ir/graph_validate.hpp>
#include <instantee/scene_ir/load.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

} // namespace

int main() {
  const std::filesystem::path root = INSTANTEE_SOURCE_DIR;
  instantee::scene_ir::LoadOptions options;
  auto document = instantee::scene_ir::load_scene_json(
      read_file(root /
                "contracts/fixtures/scene-ir/positive/minimal.scene.json"),
      options);
  expect(document.has_value(), "load fixture");
  if (!document) {
    return EXIT_FAILURE;
  }

  // Cyclic parents: headline -> main_cam -> headline
  document->nodes[0].parent_id = "main_cam";
  document->nodes[1].parent_id = "headline";
  const auto cyclic = instantee::scene_ir::validate_graph(*document);
  expect(!cyclic.has_value(), "reject cyclic parents");
  if (!cyclic) {
    expect(cyclic.error().code == "cyclic_parent", "cyclic_parent code");
  }

  document = instantee::scene_ir::load_scene_json(
      read_file(root /
                "contracts/fixtures/scene-ir/positive/minimal.scene.json"),
      options);
  expect(document.has_value(), "reload fixture");
  if (document) {
    document->nodes[0].asset_id = "missing-asset";
    const auto dangling = instantee::scene_ir::validate_graph(*document);
    expect(!dangling.has_value(), "reject dangling asset");
    if (!dangling) {
      expect(dangling.error().code == "dangling_reference",
             "dangling_reference code");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
