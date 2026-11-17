#include <instantee/direction/composition_constraints.hpp>
#include <instantee/typography/text_shape.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

using instantee::direction::CompositionConstraint;
using instantee::direction::CompositionConstraintKind;
using instantee::direction::ConstraintSeverity;
using instantee::direction::LayoutElement;
using instantee::direction::solve_composition;
using instantee::direction::TextMetrics;
using instantee::typography::FontStack;
using instantee::typography::TextShaper;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] TextMetrics
metrics_from_shape(const instantee::typography::ShapedText &shaped,
                   float size_px) {
  TextMetrics metrics;
  metrics.width_px = static_cast<double>(shaped.total_x_advance_26_6) / 64.0;
  // Approximate em-box height from requested size; ascent/descent split 80/20.
  metrics.height_px = static_cast<double>(size_px);
  metrics.ascent_px = metrics.height_px * 0.8;
  metrics.descent_px = metrics.height_px * 0.2;
  return metrics;
}

} // namespace

int main() {
  const std::filesystem::path font_dir{INSTANTEE_PINNED_FONT_DIR};
  const std::filesystem::path paths[] = {font_dir / "NotoSans-Regular.ttf",
                                         font_dir / "DejaVuSans.ttf"};
  auto fonts = FontStack::open(paths);
  expect(fonts.has_value(), "open fonts");
  if (!fonts) {
    return EXIT_FAILURE;
  }

  TextShaper shaper;
  constexpr float kSize = 32.0f;
  auto shaped = shaper.shape(*fonts, "TECH HAS CHANGED", kSize);
  expect(shaped.has_value(), "shape headline");
  if (!shaped) {
    return EXIT_FAILURE;
  }

  const TextMetrics metrics = metrics_from_shape(*shaped, kSize);
  expect(metrics.width_px > 0.0 && metrics.height_px >= kSize - 0.1,
         "real metrics positive");

  LayoutElement headline{.id = "headline",
                         .x_px = 40,
                         .y_px = 80,
                         .width_px = metrics.width_px,
                         .height_px = metrics.height_px,
                         .importance = 0.95,
                         .keep_visible = true,
                         .text = metrics};

  // Hard legibility with threshold below measured height succeeds.
  CompositionConstraint legible{.kind =
                                    CompositionConstraintKind::text_legibility,
                                .severity = ConstraintSeverity::hard,
                                .element_id = "headline",
                                .min_text_height_px = 24.0};
  CompositionConstraint bounds{.kind = CompositionConstraintKind::keep_visible,
                               .severity = ConstraintSeverity::hard,
                               .element_id = "headline"};

  auto ok = solve_composition(std::array{headline}, std::array{legible, bounds},
                              1080, 1920);
  expect(ok.has_value(), "shaped metrics satisfy hard legibility");
  if (ok) {
    expect(ok->warnings.empty(), "no soft warnings");
  }

  // Raise threshold above measured height → hard failure using same metrics.
  CompositionConstraint too_tall = legible;
  too_tall.min_text_height_px = metrics.height_px + 8.0;
  auto failed =
      solve_composition(std::array{headline}, std::array{too_tall}, 1080, 1920);
  expect(!failed.has_value(), "hard legibility fails on real metrics");
  if (!failed) {
    expect(failed.error().code == "unsatisfiable_hard_constraint",
           "diagnostics code");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
