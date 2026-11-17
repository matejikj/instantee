#include <instantee/direction/layout_lint.hpp>
#include <instantee/direction/style_bible.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using instantee::direction::LayoutLintElement;
using instantee::direction::LayoutLintFrame;
using instantee::direction::LayoutLintLimits;
using instantee::direction::lint_layout;
using instantee::direction::lint_rects_overlap;
using instantee::direction::lint_severity_id;
using instantee::direction::LintFinding;
using instantee::direction::LintRect;
using instantee::direction::LintSeverity;
using instantee::direction::SafeAreaMode;
using instantee::direction::SafeAreaPolicy;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] LayoutLintLimits default_limits() {
  LayoutLintLimits limits;
  limits.canvas_width_px = 1080;
  limits.canvas_height_px = 1920;
  limits.important_threshold = 0.7;
  limits.safe_area = SafeAreaPolicy{.mode = SafeAreaMode::reels_overlay,
                                    .top_px = 120,
                                    .bottom_px = 220,
                                    .left_px = 32,
                                    .right_px = 32};
  return limits;
}

[[nodiscard]] bool has_code(const std::vector<LintFinding> &findings,
                            std::string_view code) {
  for (const LintFinding &f : findings) {
    if (f.code == code) {
      return true;
    }
  }
  return false;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: layout_lint_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  expect(std::filesystem::exists(
             root /
             "contracts/fixtures/layout-lint/positive/clear-safe-layout.json"),
         "positive fixture");
  expect(std::filesystem::exists(
             root /
             "contracts/fixtures/layout-lint/negative/overlap-and-gutter.json"),
         "negative fixture");

  expect(lint_rects_overlap(
             {.x_px = 0, .y_px = 0, .width_px = 10, .height_px = 10},
             {.x_px = 5, .y_px = 5, .width_px = 10, .height_px = 10}),
         "overlap helper true");
  expect(!lint_rects_overlap(
             {.x_px = 0, .y_px = 0, .width_px = 10, .height_px = 10},
             {.x_px = 10, .y_px = 0, .width_px = 10, .height_px = 10}),
         "touching edges are not an overlap");

  // Positive fixture: important content inside safe rect; deco may sit in
  // gutter.
  {
    LayoutLintFrame frame;
    frame.frame = 0;
    frame.elements.push_back(LayoutLintElement{
        .id = "headline",
        .path = "/shots/0/nodes/headline",
        .bounds = {.x_px = 64, .y_px = 200, .width_px = 800, .height_px = 80},
        .importance = 0.95,
        .keep_visible = true});
    frame.elements.push_back(LayoutLintElement{
        .id = "deco",
        .path = "/shots/0/nodes/deco",
        .bounds = {.x_px = 0, .y_px = 0, .width_px = 80, .height_px = 40},
        .importance = 0.2});
    auto findings = lint_layout(std::array{frame}, default_limits());
    expect(findings.has_value() && findings->empty(),
           "clear layout has no findings");
  }

  // Negative fixture: hard overlap of important elements + safe-area/canvas.
  {
    LayoutLintFrame frame;
    frame.frame = 12;
    frame.elements.push_back(LayoutLintElement{
        .id = "title",
        .path = "/shots/0/nodes/title",
        .bounds = {.x_px = 48, .y_px = 40, .width_px = 400, .height_px = 80},
        .importance = 0.95,
        .keep_visible = true});
    frame.elements.push_back(LayoutLintElement{
        .id = "cta",
        .path = "/shots/0/nodes/cta",
        .bounds = {.x_px = 100, .y_px = 60, .width_px = 360, .height_px = 70},
        .importance = 0.9});
    frame.elements.push_back(LayoutLintElement{.id = "badge",
                                               .path = "/shots/0/nodes/badge",
                                               .bounds = {.x_px = 1000,
                                                          .y_px = 2000,
                                                          .width_px = 120,
                                                          .height_px = 40},
                                               .importance = 0.85,
                                               .keep_visible = true});

    auto findings = lint_layout(std::array{frame}, default_limits());
    expect(findings.has_value(), "negative layout lints");
    if (findings) {
      expect(has_code(*findings, "hard_overlap"), "hard overlap found");
      expect(has_code(*findings, "important_outside_safe_area"),
             "safe-area violation found");
      expect(has_code(*findings, "important_outside_canvas"),
             "canvas violation found");

      for (const LintFinding &f : *findings) {
        expect(!f.code.empty() && !f.path.empty() && !f.element_id.empty(),
               "stable finding fields");
        expect(f.severity == LintSeverity::error,
               "collision findings are errors");
        expect(std::string(lint_severity_id(f.severity)) == "error",
               "severity id");
      }

      // No aesthetic absolute claim codes.
      for (const LintFinding &f : *findings) {
        expect(f.code != "ugly" && f.code != "bad_composition",
               "no aesthetic absolute codes");
      }

      auto again = lint_layout(std::array{frame}, default_limits());
      expect(again.has_value() && again->size() == findings->size(),
             "deterministic count");
      if (again && again->size() == findings->size()) {
        for (std::size_t i = 0; i < findings->size(); ++i) {
          expect((*findings)[i].code == (*again)[i].code &&
                     (*findings)[i].path == (*again)[i].path &&
                     (*findings)[i].element_id == (*again)[i].element_id,
                 "deterministic order");
        }
      }

      // Low-importance-only overlap is not a hard finding.
      LayoutLintFrame soft;
      soft.frame = 1;
      soft.elements.push_back(LayoutLintElement{
          .id = "a",
          .path = "/shots/0/nodes/a",
          .bounds = {.x_px = 100, .y_px = 300, .width_px = 50, .height_px = 50},
          .importance = 0.1});
      soft.elements.push_back(LayoutLintElement{
          .id = "b",
          .path = "/shots/0/nodes/b",
          .bounds = {.x_px = 120, .y_px = 310, .width_px = 50, .height_px = 50},
          .importance = 0.2});
      auto soft_findings = lint_layout(std::array{soft}, default_limits());
      expect(soft_findings.has_value() && soft_findings->empty(),
             "unimportant overlap is not hard_overlap");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
