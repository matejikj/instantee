#include <instantee/direction/news_scene_templates.hpp>
#include <instantee/direction/style_bible.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main(int argc, char **argv) {
  using namespace instantee::direction;

  if (argc != 2) {
    std::cerr << "usage: news_scene_templates_test <repo-root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  const auto authoring_path = root /
                              "contracts/fixtures/news-templates/positive/"
                              "editorial-news-reel.authoring.json";

  auto doc = load_news_reel_authoring_file(authoring_path);
  expect(doc.has_value(), "load authoring example");
  if (!doc) {
    std::cerr << doc.error().code << ": " << doc.error().message << '\n';
    return EXIT_FAILURE;
  }
  expect(doc->primary.scenes.size() == 5, "five primary scenes");
  expect(doc->variant_contents.size() == 5, "five variant overlays");
  expect(!doc->style_bible_path.empty(), "style bible path");

  auto bible = load_style_bible_file(root / doc->style_bible_path);
  expect(bible.has_value(), "load style bible from authoring path");
  if (!bible) {
    return EXIT_FAILURE;
  }

  auto primary = assemble_news_reel_from_style(*bible, doc->primary);
  expect(primary.has_value(), "assemble primary news reel");
  if (!primary) {
    std::cerr << primary.error().code << ": " << primary.error().message
              << '\n';
    return EXIT_FAILURE;
  }

  expect(!primary->has_audio, "templates are silent");
  expect(primary->scenes.size() == 5, "assembled five scenes");
  expect(primary->transitions.size() == 4, "four inter-scene transitions");
  expect(primary->total_duration_frames == 120 + 180 + 240 + 210 + 150,
         "packed duration");
  expect(primary->style_package_id == bible->package_id(), "style package");
  expect(primary->beat_schedule.total_duration_frames ==
             primary->total_duration_frames,
         "beats match film budget");

  expect(primary->scenes[0].kind == NewsSceneKind::hook, "hook");
  expect(primary->scenes[1].kind == NewsSceneKind::hero_claim, "hero");
  expect(primary->scenes[2].kind == NewsSceneKind::data_comparison, "compare");
  expect(primary->scenes[3].kind == NewsSceneKind::source_screenshot_crop,
         "screenshot");
  expect(primary->scenes[4].kind == NewsSceneKind::closing_cta, "cta");

  expect(news_scene_slot(primary->scenes[0], "top_headline") != nullptr,
         "hook headline slot");
  expect(news_scene_slot(primary->scenes[2], "chart") != nullptr,
         "comparison chart slot");
  expect(news_scene_slot(primary->scenes[3], "chart") != nullptr,
         "screenshot crop frame slot");
  expect(news_scene_slot(primary->scenes[4], "bottom_space") != nullptr,
         "cta bottom slot");
  expect(news_scene_slot(primary->scenes[0], "chart") == nullptr,
         "hook does not activate chart");

  expect(primary->scenes[2].content.comparison.has_value(), "comparison data");
  expect(primary->scenes[3].content.screenshot.has_value(), "screenshot data");
  expect(primary->scenes[4].content.supporting == "Tap for the full brief",
         "cta label");

  // Existing transition presets selected — not rewritten.
  expect(primary->transitions[0].preset == EditorialTransitionPreset::crop_wipe,
         "wipe preset reused");
  expect(primary->transitions[1].preset ==
             EditorialTransitionPreset::rapid_zoom,
         "zoom preset reused");
  expect(primary->transitions[2].preset ==
             EditorialTransitionPreset::camera_snap,
         "snap preset reused");
  expect(primary->transitions[3].preset == EditorialTransitionPreset::hard_cut,
         "cut preset reused");
  for (const auto &tr : primary->transitions) {
    auto program = compile_editorial_transition(tr);
    expect(program.has_value(), "existing transition library accepts request");
  }

  // New content with the same templates/transitions/layout provenance.
  NewsReelTemplateRequest variant_req = doc->primary;
  for (std::size_t i = 0; i < variant_req.scenes.size(); ++i) {
    variant_req.scenes[i].content = doc->variant_contents[i];
  }
  variant_req.film_id = "editorial-news-reel-variant";
  auto variant = assemble_news_reel_from_style(*bible, variant_req);
  expect(variant.has_value(), "assemble variant without renderer changes");
  if (!variant) {
    return EXIT_FAILURE;
  }
  expect(variant->scenes[0].content.headline !=
             primary->scenes[0].content.headline,
         "content swapped");
  expect(variant->scenes[0].layout.content.y_px ==
             primary->scenes[0].layout.content.y_px,
         "style layout unchanged");
  expect(variant->transitions.size() == primary->transitions.size(),
         "same transition count");
  for (std::size_t i = 0; i < variant->transitions.size(); ++i) {
    expect(variant->transitions[i].preset == primary->transitions[i].preset,
           "transition presets unchanged across content swap");
  }
  expect(!variant->has_audio, "variant remains silent");

  // Rejection paths.
  {
    NewsReelTemplateRequest bad = doc->primary;
    bad.scenes[2].content.comparison.reset();
    expect(!assemble_news_reel_from_style(*bible, bad),
           "reject comparison without payload");
  }
  {
    NewsReelTemplateRequest bad = doc->primary;
    bad.scenes[0].outgoing_transition = EditorialTransitionPreset::crop_wipe;
    bad.scenes[0].outgoing_transition_frames = 0;
    expect(!assemble_news_reel_from_style(*bible, bad),
           "reject wipe with zero frames");
  }
  {
    auto specs = default_news_reel_scene_specs();
    expect(specs.size() == 5, "default specs");
    expect(specs[0].kind == NewsSceneKind::hook, "default starts with hook");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "news_scene_templates_test: ok\n";
  return EXIT_SUCCESS;
}
