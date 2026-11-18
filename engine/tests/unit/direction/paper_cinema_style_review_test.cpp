#include <instantee/direction/style_bible.hpp>
#include <instantee/direction/style_package_registry.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

using instantee::direction::load_style_bible_file;
using instantee::direction::make_builtin_style_package_registry;
using instantee::direction::make_default_engine_capabilities;
using instantee::direction::resolve_style_bible_epoch;
using instantee::direction::SafeAreaMode;
using instantee::direction::StyleBible;

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
    std::cerr << "usage: paper_cinema_style_review_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const auto bible_path =
      root / "contracts/fixtures/style-bible/positive/paper-cinema.style.json";
  const auto drift_path = root /
                          "contracts/fixtures/style-bible/semantic-negative/"
                          "epoch-renderer-drift.style.json";

  auto bible = load_style_bible_file(bible_path);
  expect(bible.has_value(), "load finalized paper-cinema style bible");
  if (!bible) {
    std::cerr << bible.error().code << ": " << bible.error().message << '\n';
    return EXIT_FAILURE;
  }

  expect(bible->package_id() == "paper-cinema", "package id");
  expect(bible->version() == "1.0.0", "package version");
  expect(bible->safe_area().mode == SafeAreaMode::reels_clear, "safe area");
  expect(bible->colors().count("pulp") == 1, "pulp");
  expect(bible->colors().count("ink") == 1, "ink");
  expect(bible->colors().count("shadow") == 1, "shadow");
  expect(bible->colors().count("wash") == 1, "wash");
  expect(bible->materials().count("paper_sheet") == 1, "paper_sheet material");
  expect(bible->materials().at("paper_sheet").shader_feature == "paper-sheet",
         "paper-sheet feature");
  expect(bible->materials().at("paper_fold").shader_feature == "paper-fold",
         "paper-fold feature");
  expect(bible->materials().at("cutout").shader_feature == "solid-fill",
         "cutout feature");
  expect(bible->materials().at("contact_shadow").shader_feature == "solid-fill",
         "contact shadow feature");
  expect(bible->spacing().count("depth_step") == 1, "depth_step spacing");
  expect(bible->spacing().count("shadow_offset") == 1, "shadow_offset spacing");
  expect(bible->motion().count("camera_move") == 1, "camera_move tempo");
  expect(bible->motion().count("layer_parallax") == 1, "layer_parallax tempo");
  expect(bible->camera().count("slow_push") == 1, "slow_push camera");
  expect(bible->camera().count("crane_pan") == 1, "crane_pan camera");

  expect(bible->epoch_ids().size() >= 3, "at least three historical epochs");
  const std::set<std::string> required_epochs = {"victorian", "wartime",
                                                 "midcentury"};
  for (const auto &id : required_epochs) {
    expect(bible->has_epoch(id), id.c_str());
  }

  auto registry = make_builtin_style_package_registry();
  const auto engine = make_default_engine_capabilities();
  auto ready = registry.check_before_render("paper-cinema", "1.0.0", engine);
  expect(ready.has_value(), "registry accepts paper-cinema");
  if (ready) {
    expect(std::find(ready->shader_features.begin(),
                     ready->shader_features.end(),
                     "paper-sheet") != ready->shader_features.end(),
           "registry lists paper-sheet");
    expect(std::find(ready->shader_features.begin(),
                     ready->shader_features.end(),
                     "solid-fill") != ready->shader_features.end(),
           "registry lists solid-fill");
    expect(std::find(ready->material_presets.begin(),
                     ready->material_presets.end(),
                     "contact_shadow") != ready->material_presets.end(),
           "registry lists contact_shadow");
  }

  std::set<std::string> pulp_signatures;
  std::set<std::uint32_t> fold_durations;
  std::set<std::string> shader_features;
  for (const auto &mat : bible->materials()) {
    shader_features.insert(mat.second.shader_feature);
  }

  for (const std::string &epoch_id : bible->epoch_ids()) {
    auto resolved = resolve_style_bible_epoch(*bible, epoch_id);
    expect(resolved.has_value(), "resolve epoch");
    if (!resolved) {
      std::cerr << "  " << epoch_id << ": " << resolved.error().code << " — "
                << resolved.error().message << '\n';
      continue;
    }
    expect(resolved->epoch_ids().empty(), "resolved snapshot has no epochs");
    expect(resolved->package_id() == bible->package_id(), "package preserved");
    expect(resolved->safe_area().mode == bible->safe_area().mode,
           "safe-area unchanged across epochs");
    expect(resolved->materials().at("paper_sheet").shader_feature ==
               "paper-sheet",
           "epoch keeps paper-sheet renderer");
    for (const auto &[id, token] : resolved->materials()) {
      expect(shader_features.contains(token.shader_feature),
             "epoch materials stay on base renderer features");
      (void)id;
    }
    const auto &pulp = resolved->colors().at("pulp").value;
    pulp_signatures.insert(std::to_string(pulp.r) + "," +
                           std::to_string(pulp.g) + "," +
                           std::to_string(pulp.b));
    fold_durations.insert(resolved->motion().at("fold_reveal").duration_frames);
    expect(resolved->materials().at("paper_sheet").parameters.count("fiber") ==
               1,
           "epoch keeps fiber contrast");
    expect(resolved->materials().at("contact_shadow").opacity > 0.0 &&
               resolved->materials().at("contact_shadow").opacity <= 1.0,
           "shadow opacity bounded");
  }

  expect(pulp_signatures.size() >= 3, "epochs differ in pulp palette");
  expect(fold_durations.size() >= 2, "epochs differ in camera/fold tempo");

  auto unknown = resolve_style_bible_epoch(*bible, "does-not-exist");
  expect(!unknown.has_value(), "unknown epoch rejected");
  if (!unknown) {
    expect(unknown.error().code == "unknown_epoch", "unknown_epoch code");
  }

  auto drift = load_style_bible_file(drift_path);
  expect(!drift.has_value(), "reject epoch renderer drift");
  if (!drift) {
    expect(drift.error().code == "epoch_renderer_drift",
           "epoch_renderer_drift code");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
