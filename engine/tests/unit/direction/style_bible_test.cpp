#include <instantee/direction/style_bible.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

using instantee::direction::load_style_bible_file;
using instantee::direction::load_style_bible_json;
using instantee::direction::resolve_style_bible_epoch;
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
  const auto notebook = root / "contracts/fixtures/style-bible/positive/"
                               "scientific-notebook.style.json";
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
  auto c = load_style_bible_file(notebook);
  expect(c.has_value(), "load scientific-notebook");
  if (!a || !b || !c) {
    return EXIT_FAILURE;
  }

  expect(a->package_id() == "editorial", "editorial package_id");
  expect(b->package_id() == "paper-cinema", "paper package_id");
  expect(c->package_id() == "scientific-notebook", "notebook package_id");
  expect(a->package_id() != b->package_id() &&
             a->package_id() != c->package_id() &&
             b->package_id() != c->package_id(),
         "three distinct packages");
  expect(a->schema_version() == "1.0.0", "schema version");
  expect(a->colors().count("accent") == 1, "editorial accent color");
  expect(b->colors().count("pulp") == 1, "paper pulp color");
  expect(c->colors().count("grid") == 1, "notebook grid color");
  expect(c->colors().count("cream") == 1, "notebook cream color");
  expect(c->colors().count("ink_blue") == 1, "notebook ink_blue color");
  expect(c->colors().count("yellow") == 1, "notebook yellow color");
  expect(c->colors().count("lavender") == 1, "notebook lavender color");
  expect(a->materials().count("halftone_panel") == 1, "editorial material");
  expect(b->materials().count("paper_sheet") == 1, "paper material");
  expect(b->materials().at("paper_sheet").shader_feature == "paper-sheet",
         "paper uses shared paper-sheet feature");
  expect(b->epoch_ids().size() >= 3, "paper declares historical epochs");
  expect(b->has_epoch("victorian"), "victorian epoch");
  auto victorian = resolve_style_bible_epoch(*b, "victorian");
  expect(victorian.has_value(), "resolve victorian");
  if (victorian) {
    expect(victorian->materials().at("paper_sheet").shader_feature ==
               "paper-sheet",
           "epoch keeps renderer");
    expect(victorian->colors().at("pulp").value.r !=
               b->colors().at("pulp").value.r,
           "epoch retints pulp");
  }
  expect(c->materials().count("pen_ink") == 1, "notebook material");
  expect(c->materials().count("highlighter") == 1, "notebook highlighter");
  expect(c->materials().count("sticky_note") == 1, "notebook sticky_note");
  expect(a->safe_area().mode == SafeAreaMode::reels_overlay, "editorial safe");
  expect(b->safe_area().mode == SafeAreaMode::reels_clear, "paper safe");
  expect(c->safe_area().mode == SafeAreaMode::full_frame, "notebook safe");
  expect(a->motion().at("headline_reveal").duration_frames == 18,
         "editorial motion");
  expect(b->camera().at("depth_orbit").framing ==
             instantee::direction::CameraFraming::orbit,
         "paper camera");
  expect(c->motion().at("ink_reveal").ease ==
             instantee::direction::MotionEase::linear,
         "notebook motion");
  expect(c->motion().at("ink_reveal").duration_frames == 36,
         "notebook learning tempo");
  expect(c->version() == "1.0.0", "notebook package version");
  expect(c->fonts().at("equation").role == instantee::direction::FontRole::mono,
         "notebook equation role");

  const auto drift = root / "contracts/fixtures/style-bible/semantic-negative/"
                            "epoch-renderer-drift.style.json";
  auto drifted = load_style_bible_file(drift);
  expect(!drifted.has_value(), "reject epoch renderer drift");
  if (!drifted) {
    expect(drifted.error().code == "epoch_renderer_drift",
           "epoch_renderer_drift code");
  }

  // Style Bible skeletons validate without requiring any core-model change:
  // colors use existing LinearRgba values. Editorial completes black / dirty
  // white / linearized #C6FF00 with a five-step type scale.
  expect(a->colors().at("ink").value.a == 1.0, "linear alpha");
  expect(a->colors().at("ink").value.r == 0.0 &&
             a->colors().at("ink").value.g == 0.0 &&
             a->colors().at("ink").value.b == 0.0,
         "editorial ink is black");
  expect(a->colors().at("accent").value.g == 1.0 &&
             a->colors().at("accent").value.b == 0.0,
         "editorial accent is acid green");
  expect(a->fonts().size() == 5, "editorial type scale size");
  expect(c->fonts().size() == 5, "notebook type scale size");

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
