#include <instantee/direction/safe_area.hpp>
#include <instantee/direction/style_bible.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using instantee::direction::content_safe_rect;
using instantee::direction::ContentSafeRect;
using instantee::direction::enforce_safe_area;
using instantee::direction::LayoutElement;
using instantee::direction::load_style_bible_file;
using instantee::direction::SafeAreaMode;
using instantee::direction::SafeAreaPolicy;
using instantee::direction::ui_safe_regions;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] bool near(double a, double b, double eps = 1e-9) {
  return std::fabs(a - b) <= eps;
}

[[nodiscard]] bool inside_rect(const LayoutElement &el,
                               const ContentSafeRect &rect) {
  return el.x_px + 1e-9 >= rect.x_px && el.y_px + 1e-9 >= rect.y_px &&
         el.x_px + el.width_px <= rect.x_px + rect.width_px + 1e-9 &&
         el.y_px + el.height_px <= rect.y_px + rect.height_px + 1e-9;
}

[[nodiscard]] const LayoutElement *
find_el(const std::vector<LayoutElement> &els, std::string_view id) {
  for (const LayoutElement &el : els) {
    if (el.id == id) {
      return &el;
    }
  }
  return nullptr;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: safe_area_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];

  const auto overlay_layout =
      root / "contracts/fixtures/safe-area/positive/reels-overlay-layout.json";
  const auto clear_layout =
      root / "contracts/fixtures/safe-area/positive/reels-clear-layout.json";
  const auto token_swap_layout =
      root / "contracts/fixtures/safe-area/positive/token-swap-layout.json";
  const auto token_swap_style =
      root /
      "contracts/fixtures/safe-area/positive/token-swap-tight.style.json";
  const auto editorial =
      root / "contracts/fixtures/style-bible/positive/editorial.style.json";
  const auto paper =
      root / "contracts/fixtures/style-bible/positive/paper-cinema.style.json";

  expect(std::filesystem::exists(overlay_layout), "overlay layout fixture");
  expect(std::filesystem::exists(clear_layout), "clear layout fixture");
  expect(std::filesystem::exists(token_swap_layout),
         "token-swap layout fixture");
  expect(std::filesystem::exists(token_swap_style), "token-swap style fixture");

  auto editorial_bible = load_style_bible_file(editorial);
  auto paper_bible = load_style_bible_file(paper);
  auto tight_bible = load_style_bible_file(token_swap_style);
  expect(editorial_bible.has_value(), "load editorial style");
  expect(paper_bible.has_value(), "load paper-cinema style");
  expect(tight_bible.has_value(), "load tight-safe style");
  if (!editorial_bible || !paper_bible || !tight_bible) {
    return EXIT_FAILURE;
  }

  // Content rect from Style Bible tokens (reels-overlay-layout fixture).
  {
    auto rect = content_safe_rect(editorial_bible->safe_area(), 1080, 1920);
    expect(rect.has_value(), "editorial content rect");
    if (rect) {
      expect(near(rect->x_px, 32) && near(rect->y_px, 120) &&
                 near(rect->width_px, 1016) && near(rect->height_px, 1580),
             "editorial content rect matches fixture");
    }
    auto regions = ui_safe_regions(editorial_bible->safe_area(), 1080, 1920);
    expect(regions.has_value() && regions->has_overlay_bands,
           "overlay UI bands present");
    if (regions) {
      expect(near(regions->top.height_px, 120), "top UI band");
      expect(near(regions->bottom.height_px, 220), "bottom UI band");
    }
  }

  // Important overlay content is pushed out of gutters; low-importance decor
  // may remain in the UI band (reels_overlay semantics).
  {
    std::vector<LayoutElement> elements{
        {.id = "title",
         .x_px = 48,
         .y_px = 24,
         .width_px = 640,
         .height_px = 72,
         .importance = 0.95,
         .keep_visible = true},
        {.id = "cta",
         .x_px = 80,
         .y_px = 1780,
         .width_px = 420,
         .height_px = 96,
         .importance = 0.9},
        {.id = "edge_decor",
         .x_px = 0,
         .y_px = 0,
         .width_px = 120,
         .height_px = 64,
         .importance = 0.2},
    };
    auto solved =
        enforce_safe_area(elements, editorial_bible->safe_area(), 1080, 1920);
    expect(solved.has_value(), "overlay enforce solves");
    if (solved) {
      auto content =
          content_safe_rect(editorial_bible->safe_area(), 1080, 1920);
      expect(content.has_value(), "overlay content for asserts");
      const LayoutElement *title = find_el(solved->elements, "title");
      const LayoutElement *cta = find_el(solved->elements, "cta");
      const LayoutElement *decor = find_el(solved->elements, "edge_decor");
      expect(title && cta && decor && content, "overlay elements present");
      if (title && cta && decor && content) {
        expect(inside_rect(*title, *content), "title clears safe area");
        expect(inside_rect(*cta, *content), "cta clears safe area");
        expect(near(decor->x_px, 0) && near(decor->y_px, 0),
               "low-importance decor may stay in gutter");
      }
    }
  }

  // reels_clear protects every element (reels-clear-layout fixture).
  {
    std::vector<LayoutElement> elements{
        {.id = "headline",
         .x_px = 20,
         .y_px = 40,
         .width_px = 500,
         .height_px = 80,
         .importance = 0.4},
        {.id = "footer_note",
         .x_px = 60,
         .y_px = 1850,
         .width_px = 300,
         .height_px = 48,
         .importance = 0.3},
    };
    auto solved =
        enforce_safe_area(elements, paper_bible->safe_area(), 1080, 1920);
    expect(solved.has_value(), "clear enforce solves");
    if (solved) {
      auto content = content_safe_rect(paper_bible->safe_area(), 1080, 1920);
      expect(content.has_value(), "clear content for asserts");
      if (content) {
        expect(near(content->x_px, 40) && near(content->y_px, 96) &&
                   near(content->width_px, 1000) &&
                   near(content->height_px, 1644),
               "paper content rect matches fixture");
        for (const LayoutElement &el : solved->elements) {
          expect(inside_rect(el, *content), "clear element inside content");
        }
      }
    }
  }

  // Token swap without engine rebuild: different Style Bible insets change the
  // content rect and the clamped title position (token-swap-layout fixture).
  {
    LayoutElement title{.id = "title",
                        .x_px = 48,
                        .y_px = 24,
                        .width_px = 640,
                        .height_px = 72,
                        .importance = 0.95,
                        .keep_visible = true};
    auto baseline_rect =
        content_safe_rect(editorial_bible->safe_area(), 1080, 1920);
    auto swapped_rect = content_safe_rect(tight_bible->safe_area(), 1080, 1920);
    expect(baseline_rect.has_value() && swapped_rect.has_value(),
           "token-swap rects");
    if (baseline_rect && swapped_rect) {
      expect(near(baseline_rect->y_px, 120) && near(swapped_rect->y_px, 160),
             "swapped top inset differs without recompile");
      expect(near(baseline_rect->height_px, 1580) &&
                 near(swapped_rect->height_px, 1480),
             "swapped bottom inset differs without recompile");
      expect(!(near(baseline_rect->x_px, swapped_rect->x_px) &&
               near(baseline_rect->y_px, swapped_rect->y_px) &&
               near(baseline_rect->width_px, swapped_rect->width_px) &&
               near(baseline_rect->height_px, swapped_rect->height_px)),
             "content rect changes with token file");
    }

    auto baseline = enforce_safe_area(std::array{title},
                                      editorial_bible->safe_area(), 1080, 1920);
    auto swapped = enforce_safe_area(std::array{title},
                                     tight_bible->safe_area(), 1080, 1920);
    expect(baseline.has_value() && swapped.has_value(), "token-swap enforce");
    if (baseline && swapped && baseline_rect && swapped_rect) {
      expect(inside_rect(baseline->elements.front(), *baseline_rect),
             "baseline title in content");
      expect(inside_rect(swapped->elements.front(), *swapped_rect),
             "swapped title in content");
      expect(swapped->elements.front().y_px >= swapped_rect->y_px - 1e-9,
             "swapped title respects tighter top token");
      expect(baseline->elements.front().y_px != swapped->elements.front().y_px,
             "layout reacts to token change");
    }
  }

  // full_frame ignores insets for content rect.
  {
    SafeAreaPolicy full{.mode = SafeAreaMode::full_frame,
                        .top_px = 200,
                        .bottom_px = 200,
                        .left_px = 40,
                        .right_px = 40};
    auto rect = content_safe_rect(full, 1080, 1920);
    expect(rect.has_value(), "full_frame rect");
    if (rect) {
      expect(near(rect->x_px, 0) && near(rect->y_px, 0) &&
                 near(rect->width_px, 1080) && near(rect->height_px, 1920),
             "full_frame uses whole canvas");
    }
  }

  // Unusable insets fail closed.
  {
    SafeAreaPolicy bad{.mode = SafeAreaMode::reels_overlay,
                       .top_px = 1000,
                       .bottom_px = 1000,
                       .left_px = 0,
                       .right_px = 0};
    auto rect = content_safe_rect(bad, 1080, 1920);
    expect(!rect.has_value(), "unusable safe area rejected");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
