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
  const auto fixture =
      root / "contracts/fixtures/scene-ir/positive/minimal.scene.json";
  const std::string text = read_file(fixture);

  instantee::scene_ir::LoadOptions options;
  const auto loaded = instantee::scene_ir::load_scene_json(text, options);
  expect(loaded.has_value(), "load minimal scene");
  if (loaded) {
    expect(loaded->schema_version == "1.0.0", "schema version");
    expect(loaded->seed == 42ULL, "seed number form");
    expect(loaded->nodes.size() == 2U, "node count");
    const auto graph = instantee::scene_ir::validate_graph(*loaded);
    expect(graph.has_value(), "graph validate minimal");
  }

  const auto bad_json = instantee::scene_ir::load_scene_json("{", options);
  expect(!bad_json.has_value(), "reject truncated JSON");
  if (!bad_json) {
    expect(bad_json.error().code == "invalid_json", "invalid_json code");
    expect(bad_json.error().context.json_path.has_value(), "json path present");
  }

  const auto unknown = instantee::scene_ir::load_scene_json(
      read_file(
          root /
          "contracts/fixtures/scene-ir/negative/unknown-field.scene.json"),
      options);
  expect(!unknown.has_value(), "reject unknown field");
  if (!unknown) {
    expect(unknown.error().code == "unknown_field", "unknown_field code");
    expect(unknown.error().context.json_path == "/surprise" ||
               unknown.error().context.json_path == "surprise",
           "unknown field path");
  }

  const auto seed_overflow = instantee::scene_ir::load_scene_json(
      read_file(root / "contracts/fixtures/scene-ir/negative/"
                       "seed-string-overflow-uint64.scene.json"),
      options);
  expect(!seed_overflow.has_value(), "reject seed overflow string");

  const auto seed_max = instantee::scene_ir::load_scene_json(
      read_file(root / "contracts/fixtures/scene-ir/positive/"
                       "seed-uint64-max-string.scene.json"),
      options);
  expect(seed_max.has_value(), "accept uint64 max seed string");
  if (seed_max) {
    expect(seed_max->seed == 18446744073709551615ULL, "uint64 max value");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
