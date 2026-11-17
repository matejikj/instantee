#include <instantee/core_model/color.hpp>
#include <instantee/direction/composition_constraints.hpp>
#include <instantee/direction/layout_lint.hpp>
#include <instantee/direction/style_bible.hpp>
#include <instantee/direction/typography_lint.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using instantee::core::LinearRgba;
using instantee::direction::CompositionConstraint;
using instantee::direction::CompositionConstraintKind;
using instantee::direction::ConstraintSeverity;
using instantee::direction::contrast_ratio;
using instantee::direction::FontRole;
using instantee::direction::LayoutElement;
using instantee::direction::LayoutLintElement;
using instantee::direction::LayoutLintFrame;
using instantee::direction::LayoutLintLimits;
using instantee::direction::lint_layout;
using instantee::direction::lint_typography;
using instantee::direction::LintFinding;
using instantee::direction::LintSeverity;
using instantee::direction::load_style_bible_file;
using instantee::direction::SafeAreaMode;
using instantee::direction::solve_composition;
using instantee::direction::StyleBible;
using instantee::direction::TextMetrics;
using instantee::direction::TypographyLintElement;
using instantee::direction::TypographyLintLimits;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] bool near(double a, double b, double eps = 1e-3) {
  return std::fabs(a - b) <= eps;
}

[[nodiscard]] LinearRgba token_color(const StyleBible &bible,
                                     std::string_view id) {
  return bible.colors().at(std::string(id)).value;
}

struct ReviewElement {
  std::string id;
  std::string path;
  double x = 0;
  double y = 0;
  double w = 0;
  double h = 0;
  double importance = 0.5;
  bool keep_visible = false;
  std::string_view fg_token = "ink";
  std::string_view bg_token = "paper";
  double font_size_px = 0;
  TextMetrics text{};
  double max_width_px = 0;
  bool has_text = false;
};

struct ReviewLayout {
  std::string_view id;
  std::string_view fixture_rel;
  std::vector<ReviewElement> elements;
};

[[nodiscard]] std::array<ReviewLayout, 5> representative_layouts() {
  return {{
      {.id = "headline-hero",
       .fixture_rel =
           "contracts/fixtures/editorial-style/layouts/01-headline-hero.json",
       .elements =
           {
               {.id = "headline",
                .path = "/layouts/headline-hero/headline",
                .x = 64,
                .y = 220,
                .w = 952,
                .h = 160,
                .importance = 0.98,
                .keep_visible = true,
                .fg_token = "ink",
                .bg_token = "paper",
                .font_size_px = 64,
                .text = {.width_px = 880,
                         .height_px = 120,
                         .ascent_px = 96,
                         .descent_px = 24},
                .max_width_px = 952,
                .has_text = true},
               {.id = "accent_bar",
                .path = "/layouts/headline-hero/accent_bar",
                .x = 64,
                .y = 400,
                .w = 160,
                .h = 12,
                .importance = 0.4,
                .fg_token = "accent",
                .bg_token = "paper"},
               {.id = "body",
                .path = "/layouts/headline-hero/body",
                .x = 64,
                .y = 440,
                .w = 900,
                .h = 200,
                .importance = 0.8,
                .keep_visible = true,
                .font_size_px = 32,
                .text = {.width_px = 860,
                         .height_px = 160,
                         .ascent_px = 128,
                         .descent_px = 32},
                .max_width_px = 900,
                .has_text = true},
           }},
      {.id = "quote-stack",
       .fixture_rel =
           "contracts/fixtures/editorial-style/layouts/02-quote-stack.json",
       .elements =
           {
               {.id = "quote",
                .path = "/layouts/quote-stack/quote",
                .x = 80,
                .y = 520,
                .w = 920,
                .h = 280,
                .importance = 0.95,
                .keep_visible = true,
                .font_size_px = 44,
                .text = {.width_px = 880,
                         .height_px = 220,
                         .ascent_px = 176,
                         .descent_px = 44},
                .max_width_px = 920,
                .has_text = true},
               {.id = "attribution",
                .path = "/layouts/quote-stack/attribution",
                .x = 80,
                .y = 820,
                .w = 640,
                .h = 48,
                .importance = 0.75,
                .keep_visible = true,
                .font_size_px = 24,
                .text = {.width_px = 420,
                         .height_px = 36,
                         .ascent_px = 28,
                         .descent_px = 8},
                .max_width_px = 640,
                .has_text = true},
           }},
      {.id = "dual-callout",
       .fixture_rel =
           "contracts/fixtures/editorial-style/layouts/03-dual-callout.json",
       .elements =
           {
               {.id = "callout_left",
                .path = "/layouts/dual-callout/callout_left",
                .x = 64,
                .y = 360,
                .w = 440,
                .h = 320,
                .importance = 0.9,
                .keep_visible = true,
                .font_size_px = 32,
                .text = {.width_px = 400,
                         .height_px = 240,
                         .ascent_px = 192,
                         .descent_px = 48},
                .max_width_px = 440,
                .has_text = true},
               {.id = "callout_right",
                .path = "/layouts/dual-callout/callout_right",
                .x = 536,
                .y = 360,
                .w = 440,
                .h = 320,
                .importance = 0.9,
                .keep_visible = true,
                .font_size_px = 32,
                .text = {.width_px = 400,
                         .height_px = 240,
                         .ascent_px = 192,
                         .descent_px = 48},
                .max_width_px = 440,
                .has_text = true},
           }},
      {.id = "photo-caption",
       .fixture_rel =
           "contracts/fixtures/editorial-style/layouts/04-photo-caption.json",
       .elements =
           {
               {.id = "photo",
                .path = "/layouts/photo-caption/photo",
                .x = 64,
                .y = 200,
                .w = 952,
                .h = 900,
                .importance = 0.85,
                .keep_visible = true},
               {.id = "caption",
                .path = "/layouts/photo-caption/caption",
                .x = 64,
                .y = 1120,
                .w = 952,
                .h = 56,
                .importance = 0.8,
                .keep_visible = true,
                .font_size_px = 28,
                .text = {.width_px = 720,
                         .height_px = 40,
                         .ascent_px = 32,
                         .descent_px = 8},
                .max_width_px = 952,
                .has_text = true},
           }},
      {.id = "acid-cta",
       .fixture_rel =
           "contracts/fixtures/editorial-style/layouts/05-acid-cta.json",
       .elements =
           {
               {.id = "body",
                .path = "/layouts/acid-cta/body",
                .x = 64,
                .y = 280,
                .w = 900,
                .h = 280,
                .importance = 0.82,
                .keep_visible = true,
                .font_size_px = 32,
                .text = {.width_px = 860,
                         .height_px = 220,
                         .ascent_px = 176,
                         .descent_px = 44},
                .max_width_px = 900,
                .has_text = true},
               {.id = "cta",
                .path = "/layouts/acid-cta/cta",
                .x = 64,
                .y = 1480,
                .w = 480,
                .h = 96,
                .importance = 0.97,
                .keep_visible = true,
                .fg_token = "ink",
                .bg_token = "accent",
                .font_size_px = 40,
                .text = {.width_px = 360,
                         .height_px = 64,
                         .ascent_px = 52,
                         .descent_px = 12},
                .max_width_px = 480,
                .has_text = true},
           }},
  }};
}

[[nodiscard]] std::size_t
error_count(const std::vector<LintFinding> &findings) {
  std::size_t n = 0;
  for (const LintFinding &f : findings) {
    if (f.severity == LintSeverity::error) {
      ++n;
    }
  }
  return n;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: editorial_style_review_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const auto bible_path =
      root / "contracts/fixtures/style-bible/positive/editorial.style.json";
  auto bible = load_style_bible_file(bible_path);
  expect(bible.has_value(), "load editorial style bible");
  if (!bible) {
    return EXIT_FAILURE;
  }

  // Brand palette: black / dirty white / #C6FF00 (linearized).
  expect(bible->colors().count("ink") == 1, "ink token");
  expect(bible->colors().count("paper") == 1, "paper token");
  expect(bible->colors().count("accent") == 1, "accent token");
  const auto ink = token_color(*bible, "ink");
  const auto paper = token_color(*bible, "paper");
  const auto accent = token_color(*bible, "accent");
  expect(near(ink.r, 0.0) && near(ink.g, 0.0) && near(ink.b, 0.0),
         "ink is black");
  expect(paper.r > 0.9 && paper.g > 0.85 && paper.b > 0.8 && paper.b < paper.r,
         "paper is dirty white");
  expect(near(accent.r, 0.5647) && near(accent.g, 1.0) && near(accent.b, 0.0),
         "accent is linearized #C6FF00");

  // Typographic scale covers every Style Bible font role.
  expect(bible->fonts().size() == 5, "five font tokens");
  expect(bible->fonts().at("display").role == FontRole::headline, "display");
  expect(bible->fonts().at("subhead").role == FontRole::subhead, "subhead");
  expect(bible->fonts().at("body").role == FontRole::body, "body");
  expect(bible->fonts().at("caption").role == FontRole::caption, "caption");
  expect(bible->fonts().at("mono").role == FontRole::mono, "mono");
  expect(bible->fonts().at("display").size_scale >
             bible->fonts().at("subhead").size_scale,
         "display larger than subhead");
  expect(bible->fonts().at("subhead").size_scale >
             bible->fonts().at("body").size_scale,
         "subhead larger than body");
  expect(bible->fonts().at("body").size_scale >
             bible->fonts().at("caption").size_scale,
         "body larger than caption");

  // Contrasts: ink/paper and ink/accent must clear large-text AA; accent/paper
  // is intentionally decorative-only (not for body text).
  expect(contrast_ratio(ink, paper) >= 4.5, "ink on paper contrast");
  expect(contrast_ratio(ink, accent) >= 4.5, "ink on accent contrast");
  expect(contrast_ratio(accent, paper) < 3.0,
         "accent on paper is not a text pair");

  expect(bible->safe_area().mode == SafeAreaMode::reels_overlay,
         "reels overlay safe area");
  expect(bible->spacing().count("column_gap") == 1, "column_gap spacing");
  expect(bible->spacing().count("accent_bar") == 1, "accent_bar spacing");
  expect(bible->materials().count("acid_block") == 1, "acid_block material");

  LayoutLintLimits layout_limits;
  layout_limits.canvas_width_px = 1080;
  layout_limits.canvas_height_px = 1920;
  layout_limits.important_threshold = 0.7;
  layout_limits.safe_area = bible->safe_area();

  TypographyLintLimits type_limits;
  type_limits.canvas_width_px = 1080;
  type_limits.canvas_height_px = 1920;
  type_limits.min_font_size_px = 24.0;

  const auto layouts = representative_layouts();
  expect(layouts.size() == 5, "five representative layouts");
  for (const ReviewLayout &layout : layouts) {
    expect(std::filesystem::exists(root / layout.fixture_rel),
           layout.fixture_rel.data());

    LayoutLintFrame frame;
    frame.frame = 0;
    std::vector<TypographyLintElement> type_elements;
    std::vector<LayoutElement> composition_elements;
    std::vector<CompositionConstraint> constraints;

    for (const ReviewElement &el : layout.elements) {
      frame.elements.push_back(LayoutLintElement{
          .id = el.id,
          .path = el.path,
          .bounds =
              {.x_px = el.x, .y_px = el.y, .width_px = el.w, .height_px = el.h},
          .importance = el.importance,
          .keep_visible = el.keep_visible});

      composition_elements.push_back(LayoutElement{
          .id = el.id,
          .x_px = el.x,
          .y_px = el.y,
          .width_px = el.w,
          .height_px = el.h,
          .importance = el.importance,
          .keep_visible = el.keep_visible,
          .text = el.has_text ? std::optional<TextMetrics>{el.text}
                              : std::nullopt});
      constraints.push_back(
          CompositionConstraint{.kind = CompositionConstraintKind::bound_inside,
                                .severity = ConstraintSeverity::hard,
                                .element_id = el.id});
      if (el.keep_visible) {
        constraints.push_back(CompositionConstraint{
            .kind = CompositionConstraintKind::keep_visible,
            .severity = ConstraintSeverity::hard,
            .element_id = el.id});
      }
      if (el.has_text) {
        constraints.push_back(CompositionConstraint{
            .kind = CompositionConstraintKind::text_legibility,
            .severity = ConstraintSeverity::hard,
            .element_id = el.id,
            .min_text_height_px = 24.0});
        type_elements.push_back(TypographyLintElement{
            .id = el.id,
            .path = el.path,
            .font_size_px = el.font_size_px,
            .bounds = {.x_px = el.x,
                       .y_px = el.y,
                       .width_px = el.w,
                       .height_px = el.h},
            .foreground = token_color(*bible, el.fg_token),
            .background = token_color(*bible, el.bg_token),
            .text = el.text,
            .max_width_px = el.max_width_px});
      }
    }

    auto layout_findings = lint_layout(std::array{frame}, layout_limits);
    expect(layout_findings.has_value(), "layout lint ok");
    if (layout_findings) {
      expect(error_count(*layout_findings) == 0, "layout respects safe area");
    }

    if (!type_elements.empty()) {
      auto type_findings = lint_typography(type_elements, type_limits);
      expect(type_findings.has_value(), "typography lint ok");
      if (type_findings) {
        expect(error_count(*type_findings) == 0,
               "typography respects tokens/contrast");
      }
    }

    auto solved =
        solve_composition(composition_elements, constraints, 1080.0, 1920.0);
    expect(solved.has_value(), "composition constraints satisfied");
  }

  // Negative control: hero headline shoved into the top Reels gutter must fail.
  {
    LayoutLintFrame bad;
    bad.frame = 0;
    bad.elements.push_back(LayoutLintElement{
        .id = "headline",
        .path = "/layouts/negative/headline",
        .bounds = {.x_px = 64, .y_px = 20, .width_px = 900, .height_px = 80},
        .importance = 0.99,
        .keep_visible = true});
    auto findings = lint_layout(std::array{bad}, layout_limits);
    expect(findings.has_value() && error_count(*findings) > 0,
           "safe-area violation is detectable");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
