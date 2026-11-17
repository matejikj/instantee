#include <instantee/direction/style_bible.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

using instantee::direction::load_style_bible_file;
using instantee::direction::load_style_bible_json;
using instantee::direction::SafeAreaMode;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: style_bible_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const auto editorial =
      root / "contracts/fixtures/style-bible/positive/editorial.style.json";
  const auto paper =
      root / "contracts/fixtures/style-bible/positive/paper-cinema.style.json";
  const auto bad_schema =
      root /
      "contracts/fixtures/style-bible/negative/unsupported-schema.style.json";
  const auto missing_safe =
      root /
      "contracts/fixtures/style-bible/negative/missing-safe-area.style.json";

  auto a = load_style_bible_file(editorial);
  expect(a.has_value(), "load editorial");
  auto b = load_style_bible_file(paper);
  expect(b.has_value(), "load paper-cinema");
  if (!a || !b) {
    return EXIT_FAILURE;
  }

  expect(a->package_id() == "editorial", "editorial package_id");
  expect(b->package_id() == "paper-cinema", "paper package_id");
  expect(a->package_id() != b->package_id(), "distinct packages");
  expect(a->schema_version() == "1.0.0", "schema version");
  expect(a->colors().count("accent") == 1, "editorial accent color");
  expect(b->colors().count("pulp") == 1, "paper pulp color");
  expect(a->materials().count("halftone_panel") == 1, "editorial material");
  expect(b->materials().count("paper_sheet") == 1, "paper material");
  expect(a->safe_area().mode == SafeAreaMode::reels_overlay, "editorial safe");
  expect(b->safe_area().mode == SafeAreaMode::reels_clear, "paper safe");
  expect(a->motion().at("headline_reveal").duration_frames == 18,
         "editorial motion");
  expect(b->camera().at("depth_orbit").framing ==
             instantee::direction::CameraFraming::orbit,
         "paper camera");

  // Two manifests validate without requiring any core-model change: colors use
  // existing LinearRgba values.
  expect(a->colors().at("ink").value.a == 1.0, "linear alpha");

  auto unsupported = load_style_bible_file(bad_schema);
  expect(!unsupported.has_value(), "reject unsupported schema");
  if (!unsupported) {
    expect(unsupported.error().code == "unsupported_schema",
           "unsupported code");
  }

  auto missing = load_style_bible_file(missing_safe);
  expect(!missing.has_value(), "reject missing safe_area");

  auto empty = load_style_bible_json("");
  expect(!empty.has_value(), "reject empty");

  auto garbage = load_style_bible_json("{");
  expect(!garbage.has_value(), "reject invalid json");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
