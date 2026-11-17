#include <instantee/direction/typography_lint.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

using instantee::core::LinearRgba;
using instantee::direction::contrast_ratio;
using instantee::direction::lint_severity_id;
using instantee::direction::lint_typography;
using instantee::direction::LintFinding;
using instantee::direction::LintSeverity;
using instantee::direction::relative_luminance;
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

[[nodiscard]] bool near(double a, double b, double eps = 1e-9) {
  return std::fabs(a - b) <= eps;
}

[[nodiscard]] TypographyLintElement legible_headline() {
  TypographyLintElement el;
  el.id = "headline";
  el.path = "/shots/0/nodes/headline";
  el.font_size_px = 48;
  el.bounds = {.x_px = 80, .y_px = 240, .width_px = 900, .height_px = 120};
  el.foreground = LinearRgba{0.02, 0.02, 0.03, 1.0};
  el.background = LinearRgba{0.96, 0.94, 0.9, 1.0};
  el.text = TextMetrics{
      .width_px = 640, .height_px = 72, .ascent_px = 58, .descent_px = 14};
  el.max_width_px = 900;
  return el;
}

[[nodiscard]] TypographyLintElement bad_caption() {
  TypographyLintElement el;
  el.id = "caption";
  el.path = "/shots/0/nodes/caption";
  el.font_size_px = 14;
  el.bounds = {.x_px = 40, .y_px = 1800, .width_px = 200, .height_px = 40};
  el.foreground = LinearRgba{0.55, 0.55, 0.55, 1.0};
  el.background = LinearRgba{0.6, 0.6, 0.6, 1.0};
  el.text = TextMetrics{
      .width_px = 320, .height_px = 36, .ascent_px = 28, .descent_px = 8};
  el.mask = instantee::direction::LintRect{
      .x_px = 40, .y_px = 1800, .width_px = 180, .height_px = 40};
  el.max_width_px = 200;
  return el;
}

[[nodiscard]] bool fixture_exists(const std::filesystem::path &root,
                                  std::string_view rel) {
  return std::filesystem::exists(root / rel);
}

[[nodiscard]] bool snapshot_has(const std::vector<LintFinding> &findings,
                                std::string_view code, LintSeverity severity,
                                std::string_view path) {
  for (const LintFinding &f : findings) {
    if (f.code == code && f.severity == severity && f.path == path &&
        !f.element_id.empty()) {
      return true;
    }
  }
  return false;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: typography_lint_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  expect(
      fixture_exists(
          root,
          "contracts/fixtures/typography-lint/positive/legible-headline.json"),
      "positive fixture present");
  expect(fixture_exists(root, "contracts/fixtures/typography-lint/negative/"
                              "small-low-contrast-clipped.json"),
         "negative fixture present");

  // Luminance/contrast helpers are monotonic and finite.
  {
    const LinearRgba black{0, 0, 0, 1};
    const LinearRgba white{1, 1, 1, 1};
    expect(near(relative_luminance(black), 0.0), "black luminance");
    expect(near(relative_luminance(white), 1.0), "white luminance");
    expect(near(contrast_ratio(black, white), 21.0, 1e-9),
           "black/white contrast is 21:1");
  }

  // Positive fixture scenario: no findings.
  {
    auto findings = lint_typography(std::array{legible_headline()});
    expect(findings.has_value() && findings->empty(),
           "legible has no findings");
  }

  // Negative fixture scenario: each warning has stable code, severity, path.
  {
    auto findings = lint_typography(std::array{bad_caption()});
    expect(findings.has_value(), "bad caption lints");
    if (findings) {
      expect(snapshot_has(*findings, "min_font_size", LintSeverity::error,
                          "/shots/0/nodes/caption"),
             "min_font_size finding");
      expect(snapshot_has(*findings, "contrast_ratio", LintSeverity::error,
                          "/shots/0/nodes/caption"),
             "contrast_ratio finding");
      expect(snapshot_has(*findings, "text_mask_clip", LintSeverity::error,
                          "/shots/0/nodes/caption"),
             "text_mask_clip finding");
      expect(snapshot_has(*findings, "text_overflow", LintSeverity::warning,
                          "/shots/0/nodes/caption"),
             "text_overflow finding");

      std::unordered_set<std::string> codes;
      for (const LintFinding &f : *findings) {
        expect(!f.code.empty() && !f.path.empty() && !f.element_id.empty(),
               "finding fields populated");
        expect(f.path == "/shots/0/nodes/caption", "stable path");
        expect(lint_severity_id(f.severity) == "error" ||
                   lint_severity_id(f.severity) == "warning",
               "severity id");
        codes.insert(f.code);
      }
      expect(codes.contains("min_font_size") &&
                 codes.contains("contrast_ratio") &&
                 codes.contains("text_mask_clip") &&
                 codes.contains("text_overflow"),
             "expected codes present");

      // Deterministic order: path, code, element_id.
      auto again = lint_typography(std::array{bad_caption()});
      expect(again.has_value() && again->size() == findings->size(),
             "deterministic count");
      if (again && again->size() == findings->size()) {
        for (std::size_t i = 0; i < findings->size(); ++i) {
          expect((*findings)[i].code == (*again)[i].code &&
                     (*findings)[i].path == (*again)[i].path &&
                     (*findings)[i].severity == (*again)[i].severity,
                 "deterministic finding order");
        }
      }
    }
  }

  // Reject invalid elements without inventing findings.
  {
    TypographyLintElement bad = legible_headline();
    bad.path.clear();
    expect(!lint_typography(std::array{bad}).has_value(),
           "empty path rejected");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
