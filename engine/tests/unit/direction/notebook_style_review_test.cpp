#include <instantee/core_model/color.hpp>
#include <instantee/direction/composition_constraints.hpp>
#include <instantee/direction/layout_lint.hpp>
#include <instantee/direction/style_bible.hpp>
#include <instantee/direction/style_package_registry.hpp>
#include <instantee/direction/typography_lint.hpp>

#include <array>
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
using instantee::direction::make_builtin_style_package_registry;
using instantee::direction::MotionEase;
using instantee::direction::SafeAreaMode;
using instantee::direction::solve_composition;
using instantee::direction::StyleBible;
using instantee::direction::StylePackageDesc;
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
  std::string_view fg_token = "ink_blue";
  std::string_view bg_token = "cream";
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
      {.id = "problem-statement",
       .fixture_rel = "contracts/fixtures/notebook-style/layouts/"
                      "01-problem-statement.json",
       .elements =
           {
               {.id = "title",
                .path = "/layouts/problem-statement/title",
                .x = 72,
                .y = 160,
                .w = 936,
                .h = 120,
                .importance = 0.96,
                .keep_visible = true,
                .font_size_px = 52,
                .text = {.width_px = 900,
                         .height_px = 96,
                         .ascent_px = 76,
                         .descent_px = 20},
                .max_width_px = 936,
                .has_text = true},
               {.id = "margin_rule",
                .path = "/layouts/problem-statement/margin_rule",
                .x = 56,
                .y = 140,
                .w = 8,
                .h = 1600,
                .importance = 0.2,
                .fg_token = "lavender",
                .bg_token = "cream"},
               {.id = "prompt",
                .path = "/layouts/problem-statement/prompt",
                .x = 88,
                .y = 320,
                .w = 900,
                .h = 220,
                .importance = 0.85,
                .keep_visible = true,
                .font_size_px = 30,
                .text = {.width_px = 860,
                         .height_px = 180,
                         .ascent_px = 140,
                         .descent_px = 40},
                .max_width_px = 900,
                .has_text = true},
           }},
      {.id = "construction",
       .fixture_rel =
           "contracts/fixtures/notebook-style/layouts/02-construction.json",
       .elements =
           {
               {.id = "diagram",
                .path = "/layouts/construction/diagram",
                .x = 96,
                .y = 280,
                .w = 640,
                .h = 720,
                .importance = 0.94,
                .keep_visible = true},
               {.id = "sticky_panel",
                .path = "/layouts/construction/sticky_panel",
                .x = 772,
                .y = 312,
                .w = 236,
                .h = 196,
                .importance = 0.35,
                .fg_token = "lavender",
                .bg_token = "cream"},
               {.id = "sticky",
                .path = "/layouts/construction/sticky",
                .x = 788,
                .y = 328,
                .w = 204,
                .h = 164,
                .importance = 0.7,
                .keep_visible = true,
                .font_size_px = 24,
                .text = {.width_px = 180,
                         .height_px = 120,
                         .ascent_px = 90,
                         .descent_px = 30},
                .max_width_px = 200,
                .has_text = true},
               {.id = "highlight",
                .path = "/layouts/construction/highlight",
                .x = 96,
                .y = 1040,
                .w = 640,
                .h = 28,
                .importance = 0.35,
                .fg_token = "yellow",
                .bg_token = "cream"},
           }},
      {.id = "derivation",
       .fixture_rel =
           "contracts/fixtures/notebook-style/layouts/03-derivation.json",
       .elements =
           {
               {.id = "step_label",
                .path = "/layouts/derivation/step_label",
                .x = 88,
                .y = 200,
                .w = 400,
                .h = 56,
                .importance = 0.75,
                .keep_visible = true,
                .font_size_px = 34,
                .text = {.width_px = 360,
                         .height_px = 44,
                         .ascent_px = 34,
                         .descent_px = 10},
                .max_width_px = 400,
                .has_text = true},
               {.id = "equation",
                .path = "/layouts/derivation/equation",
                .x = 88,
                .y = 280,
                .w = 880,
                .h = 96,
                .importance = 0.95,
                .keep_visible = true,
                .font_size_px = 36,
                .text = {.width_px = 820,
                         .height_px = 72,
                         .ascent_px = 56,
                         .descent_px = 16},
                .max_width_px = 880,
                .has_text = true},
               {.id = "annotation",
                .path = "/layouts/derivation/annotation",
                .x = 88,
                .y = 400,
                .w = 720,
                .h = 80,
                .importance = 0.65,
                .keep_visible = true,
                .font_size_px = 24,
                .text = {.width_px = 680,
                         .height_px = 60,
                         .ascent_px = 46,
                         .descent_px = 14},
                .max_width_px = 720,
                .has_text = true},
           }},
      {.id = "visual-intuition",
       .fixture_rel = "contracts/fixtures/notebook-style/layouts/"
                      "04-visual-intuition.json",
       .elements =
           {
               {.id = "insight",
                .path = "/layouts/visual-intuition/insight",
                .x = 72,
                .y = 240,
                .w = 936,
                .h = 100,
                .importance = 0.92,
                .keep_visible = true,
                .font_size_px = 48,
                .text = {.width_px = 880,
                         .height_px = 80,
                         .ascent_px = 62,
                         .descent_px = 18},
                .max_width_px = 936,
                .has_text = true},
               {.id = "sketch",
                .path = "/layouts/visual-intuition/sketch",
                .x = 120,
                .y = 400,
                .w = 840,
                .h = 640,
                .importance = 0.9,
                .keep_visible = true},
               {.id = "callout_bar",
                .path = "/layouts/visual-intuition/callout_bar",
                .x = 120,
                .y = 1080,
                .w = 240,
                .h = 16,
                .importance = 0.3,
                .fg_token = "yellow",
                .bg_token = "cream"},
           }},
      {.id = "summary",
       .fixture_rel =
           "contracts/fixtures/notebook-style/layouts/05-summary.json",
       .elements =
           {
               {.id = "summary_title",
                .path = "/layouts/summary/summary_title",
                .x = 72,
                .y = 200,
                .w = 700,
                .h = 64,
                .importance = 0.88,
                .keep_visible = true,
                .font_size_px = 40,
                .text = {.width_px = 660,
                         .height_px = 52,
                         .ascent_px = 40,
                         .descent_px = 12},
                .max_width_px = 700,
                .has_text = true},
               {.id = "takeaways",
                .path = "/layouts/summary/takeaways",
                .x = 72,
                .y = 300,
                .w = 720,
                .h = 360,
                .importance = 0.9,
                .keep_visible = true,
                .font_size_px = 28,
                .text = {.width_px = 680,
                         .height_px = 300,
                         .ascent_px = 230,
                         .descent_px = 70},
                .max_width_px = 720,
                .has_text = true},
               {.id = "reminder_panel",
                .path = "/layouts/summary/reminder_panel",
                .x = 812,
                .y = 272,
                .w = 216,
                .h = 236,
                .importance = 0.35,
                .fg_token = "lavender",
                .bg_token = "cream"},
               {.id = "reminder",
                .path = "/layouts/summary/reminder",
                .x = 828,
                .y = 288,
                .w = 184,
                .h = 204,
                .importance = 0.7,
                .keep_visible = true,
                .font_size_px = 24,
                .text = {.width_px = 160,
                         .height_px = 160,
                         .ascent_px = 120,
                         .descent_px = 40},
                .max_width_px = 180,
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
    std::cerr << "usage: notebook_style_review_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const auto bible_path = root / "contracts/fixtures/style-bible/positive/"
                                 "scientific-notebook.style.json";
  auto bible = load_style_bible_file(bible_path);
  expect(bible.has_value(), "load scientific-notebook style bible");
  if (!bible) {
    return EXIT_FAILURE;
  }

  expect(bible->package_id() == "scientific-notebook", "package_id");
  expect(bible->version() == "1.0.0", "package version");
  expect(bible->schema_version() == "1.0.0", "schema version");

  // Learning palette: cream page, ink blue, highlighter yellow, lavender.
  expect(bible->colors().count("cream") == 1, "cream token");
  expect(bible->colors().count("page") == 1, "page token");
  expect(bible->colors().count("ink_blue") == 1, "ink_blue token");
  expect(bible->colors().count("yellow") == 1, "yellow token");
  expect(bible->colors().count("lavender") == 1, "lavender token");
  expect(bible->colors().count("grid") == 1, "grid token");
  const auto cream = token_color(*bible, "cream");
  const auto ink_blue = token_color(*bible, "ink_blue");
  const auto yellow = token_color(*bible, "yellow");
  const auto lavender = token_color(*bible, "lavender");
  expect(cream.r > 0.9 && cream.g > 0.9 && cream.b > 0.8 && cream.b < cream.r,
         "cream is warm page");
  expect(ink_blue.b > ink_blue.r && ink_blue.b > ink_blue.g &&
             ink_blue.r < 0.15,
         "ink_blue is deep blue");
  expect(yellow.r > 0.85 && yellow.g > 0.7 && yellow.b < 0.35,
         "yellow is highlighter");
  expect(lavender.b > lavender.g && lavender.r > 0.5 && lavender.b > 0.75,
         "lavender accent");

  expect(bible->fonts().size() == 5, "five font tokens");
  expect(bible->fonts().at("display").role == FontRole::headline, "display");
  expect(bible->fonts().at("subhead").role == FontRole::subhead, "subhead");
  expect(bible->fonts().at("body").role == FontRole::body, "body");
  expect(bible->fonts().at("caption").role == FontRole::caption, "caption");
  expect(bible->fonts().at("equation").role == FontRole::mono, "equation");
  expect(bible->fonts().at("display").size_scale >
             bible->fonts().at("subhead").size_scale,
         "display larger than subhead");
  expect(bible->fonts().at("subhead").size_scale >
             bible->fonts().at("body").size_scale,
         "subhead larger than body");

  expect(contrast_ratio(ink_blue, cream) >= 4.5, "ink_blue on cream contrast");
  expect(contrast_ratio(yellow, cream) < 3.0,
         "yellow on cream is decorative-only");
  expect(contrast_ratio(lavender, cream) < 3.0,
         "lavender on cream is decorative-only");

  expect(bible->safe_area().mode == SafeAreaMode::full_frame,
         "full_frame safe area");
  expect(bible->spacing().count("margin_rule") == 1, "margin_rule spacing");
  expect(bible->spacing().count("equation_pad") == 1, "equation_pad spacing");
  expect(bible->materials().count("pen_ink") == 1, "pen_ink material");
  expect(bible->materials().count("notebook_grid") == 1, "notebook_grid");
  expect(bible->materials().count("highlighter") == 1, "highlighter material");
  expect(bible->materials().count("sticky_note") == 1, "sticky_note material");

  // Learning tempo is deliberately slower than Editorial headline reveals.
  expect(bible->motion().at("ink_reveal").duration_frames == 36, "ink_reveal");
  expect(bible->motion().at("ink_reveal").ease == MotionEase::linear,
         "ink_reveal ease");
  expect(bible->motion().at("step_reveal").duration_frames == 28,
         "step_reveal");
  expect(bible->motion().at("highlight_draw").duration_frames == 22,
         "highlight_draw");
  expect(bible->motion().at("diagram_settle").duration_frames == 30,
         "diagram_settle");
  expect(bible->camera().count("page_hold") == 1, "page_hold camera");
  expect(bible->camera().count("detail_push") == 1, "detail_push camera");
  expect(bible->camera().count("step_pan") == 1, "step_pan camera");

  // Versionability: registry pins the published revision; outdated and
  // unknown revisions fail closed; a new semver revision can be registered.
  auto registry = make_builtin_style_package_registry();
  expect(registry.contains("scientific-notebook", "1.0.0"),
         "registry has 1.0.0");
  auto current = registry.resolve("scientific-notebook", "1.0.0");
  expect(current.has_value(), "resolve current notebook revision");
  if (current) {
    expect(current->material_presets.size() >= 4, "notebook materials pinned");
  }
  auto outdated = registry.resolve("scientific-notebook", "0.9.0");
  expect(!outdated.has_value(), "outdated notebook revision rejected");
  auto next = registry.register_package(StylePackageDesc{
      .package_id = "scientific-notebook",
      .version = "1.0.1",
      .minimum_engine_version = "0.1.0",
      .required_capabilities = {"path_geometry", "text", "camera", "tracks",
                                "export", "typography", "time"},
      .registered_components = {"ink_stroke", "math_layout", "diagram_note",
                                "sticky_note", "highlighter"},
      .shader_features = {"ink-stroke", "grid-paper", "solid-fill"},
      .material_presets = {"pen_ink", "notebook_grid", "highlighter",
                           "sticky_note"},
  });
  expect(next.has_value(), "register next notebook revision");
  expect(registry.contains("scientific-notebook", "1.0.1"),
         "registry retains both revisions");
  expect(registry.versions_for("scientific-notebook").size() == 2,
         "two notebook versions");

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

  // Negative control: full_frame ignores UI gutters, so detect off-canvas
  // important content instead.
  {
    LayoutLintFrame bad;
    bad.frame = 0;
    bad.elements.push_back(LayoutLintElement{
        .id = "title",
        .path = "/layouts/negative/title",
        .bounds = {.x_px = 72, .y_px = -40, .width_px = 900, .height_px = 80},
        .importance = 0.99,
        .keep_visible = true});
    auto findings = lint_layout(std::array{bad}, layout_limits);
    expect(findings.has_value() && error_count(*findings) > 0,
           "off-canvas important content is detectable");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
