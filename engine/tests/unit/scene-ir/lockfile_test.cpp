#include <instantee/scene_ir/load.hpp>
#include <instantee/scene_ir/lockfile.hpp>

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
  expect(document.has_value(), "load scene");
  if (!document) {
    return EXIT_FAILURE;
  }

  auto lockfile = instantee::scene_ir::make_lockfile_from_scene(
      *document, "metal-apple-baseline");
  auto finalized = instantee::scene_ir::finalize_lockfile(lockfile);
  expect(finalized.has_value(), "finalize lockfile");
  if (!finalized) {
    return EXIT_FAILURE;
  }
  expect(finalized->content_sha256.size() == 64U, "content hash length");
  expect(!finalized->assets.empty(), "assets recorded");
  expect(finalized->assets.front().role == "font", "font role");

  expect(
      instantee::scene_ir::lockfile_invalidated_by_asset_change(
          *finalized, finalized->assets.front().id,
          "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
      "asset content change invalidates lockfile");

  expect(!instantee::scene_ir::lockfile_invalidated_by_asset_change(
             *finalized, finalized->assets.front().id,
             finalized->assets.front().sha256),
         "same hash does not invalidate");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
