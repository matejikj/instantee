#include <instantee/scene_ir/authoring.hpp>
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

  auto cyclic = instantee::scene_ir::load_scene_json(
      read_file(root / "contracts/fixtures/scene-ir/semantic-negative/"
                       "cyclic-parent.scene.json"),
      options);
  expect(cyclic.has_value(), "load cyclic fixture");
  if (cyclic) {
    auto graph = instantee::scene_ir::validate_graph(*cyclic);
    expect(!graph.has_value(), "reject cyclic parents from fixture");
    if (!graph) {
      expect(graph.error().code == "cyclic_parent", "cyclic_parent code");
    }
  }

  auto dangling = instantee::scene_ir::load_scene_json(
      read_file(root / "contracts/fixtures/scene-ir/semantic-negative/"
                       "dangling-ref.scene.json"),
      options);
  expect(dangling.has_value(), "load dangling fixture");
  if (dangling) {
    auto graph = instantee::scene_ir::validate_graph(*dangling);
    expect(!graph.has_value(), "reject dangling font from fixture");
    if (!graph) {
      expect(graph.error().code == "dangling_reference",
             "dangling_reference code");
    }
  }

  auto unicode = instantee::scene_ir::load_authoring_json(
      read_file(
          root /
          "contracts/fixtures/authoring/positive/unicode-ids.authoring.json"),
      options);
  expect(unicode.has_value(), "load unicode authoring");
  if (unicode) {
    expect(unicode->film_id == "film-日本語", "unicode film_id");
    expect(unicode->shots.at(0).id == "záber-1", "unicode shot id");
    expect(unicode->shots.at(0).nodes.at(0).id == "nadpis-α",
           "unicode node id");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
