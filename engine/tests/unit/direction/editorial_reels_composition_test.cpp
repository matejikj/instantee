#include <instantee/direction/editorial_reels_composition.hpp>
#include <instantee/direction/style_bible.hpp>

#include <cmath>
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

[[nodiscard]] const instantee::direction::LayoutElement *
find_slot(const instantee::direction::ReelsCompositionLayout &layout,
          std::string_view id) {
  for (const auto &el : layout.elements) {
    if (el.id == id) {
      return &el;
    }
  }
  return nullptr;
}

} // namespace

int main(int argc, char **argv) {
  using namespace instantee::direction;

  if (argc != 2) {
    std::cerr << "usage: editorial_reels_composition_test <repo-root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};

  auto editorial = load_style_bible_file(
      root / "contracts/fixtures/style-bible/positive/editorial.style.json");
  expect(editorial.has_value(), "load editorial style");
  if (!editorial) {
    return EXIT_FAILURE;
  }

  auto tight = load_style_bible_file(
      root /
      "contracts/fixtures/safe-area/positive/token-swap-tight.style.json");
  expect(tight.has_value(), "load tight safe-area style");
  if (!tight) {
    return EXIT_FAILURE;
  }

  // Baseline Reels layout from versioned editorial tokens.
  auto base_req =
      reels_composition_request_from_style(*editorial, 1080.0, 1920.0);
  expect(base_req.style_package_id == editorial->package_id(),
         "package provenance");
  expect(base_req.style_version == editorial->version(), "version provenance");
  expect(base_req.safe_area.top_px == editorial->safe_area().top_px,
         "safe-area from style token");
  expect(base_req.stack_gap_px > 0.0, "stack spacing from token");

  auto base = compose_editorial_reels(base_req);
  expect(base.has_value(), "compose baseline reels");
  if (!base) {
    std::cerr << base.error().code << ": " << base.error().message << '\n';
    return EXIT_FAILURE;
  }
  expect(base->elements.size() == 4, "four slots");
  expect(reels_composition_respects_safe_area(*base),
         "all slots inside content-safe");
  expect(base->style_version == editorial->version(), "layout version tag");

  const auto *headline = find_slot(*base, "top_headline");
  const auto *callout = find_slot(*base, "callout");
  const auto *chart = find_slot(*base, "chart");
  const auto *bottom = find_slot(*base, "bottom_space");
  expect(headline && callout && chart && bottom, "named slots present");
  if (headline && callout && chart && bottom) {
    expect(headline->y_px == base->content.y_px, "headline at content top");
    expect(callout->y_px > headline->y_px + headline->height_px - 1e-9,
           "callout below headline");
    expect(chart->y_px > callout->y_px + callout->height_px - 1e-9,
           "chart below callout");
    expect(bottom->y_px > chart->y_px + chart->height_px - 1e-9,
           "bottom below chart");
    expect(callout->width_px < headline->width_px, "callout narrower chip");
    expect(callout->x_px + callout->width_px <=
               base->content.x_px + base->content.width_px + 1e-6,
           "callout inside content");
    expect(
        std::fabs((headline->height_px + callout->height_px + chart->height_px +
                   bottom->height_px + 3.0 * base_req.stack_gap_px) -
                  base->content.height_px) < 1e-6,
        "slots + gaps fill content height");
  }

  // Versioned safe-area token swap relocates the composition without changing
  // ratio presets (acceptance: safe-area via versioned token).
  expect(tight->version() != editorial->version() ||
             tight->safe_area().top_px != editorial->safe_area().top_px ||
             tight->safe_area().bottom_px != editorial->safe_area().bottom_px,
         "fixtures differ in safe-area or version");

  auto swapped_req =
      reels_composition_request_from_style(*tight, 1080.0, 1920.0);
  auto swapped = compose_editorial_reels(swapped_req);
  expect(swapped.has_value(), "compose after token swap");
  if (!swapped) {
    return EXIT_FAILURE;
  }
  expect(reels_composition_respects_safe_area(*swapped),
         "swapped layout respects safe-area");
  expect(swapped->content.y_px != base->content.y_px ||
             swapped->content.height_px != base->content.height_px,
         "token swap changes content-safe rect");

  const auto *sw_headline = find_slot(*swapped, "top_headline");
  expect(sw_headline != nullptr, "swapped headline");
  if (sw_headline && headline) {
    expect(sw_headline->y_px != headline->y_px,
           "versioned token moves top headline");
  }

  // Reject invalid inputs.
  {
    ReelsCompositionRequest bad = base_req;
    bad.canvas_width_px = 0.0;
    expect(!compose_editorial_reels(bad), "reject zero canvas");
  }
  {
    ReelsCompositionRequest bad = base_req;
    bad.ratios.top_headline = 0.5;
    bad.ratios.callout = 0.3;
    bad.ratios.chart = 0.3; // sum >= 1
    expect(!compose_editorial_reels(bad), "reject ratio overflow");
  }
  {
    ReelsCompositionRequest bad = base_req;
    bad.stack_gap_px = -1.0;
    expect(!compose_editorial_reels(bad), "reject negative spacing");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "editorial_reels_composition_test: ok\n";
  return EXIT_SUCCESS;
}
