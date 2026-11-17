#include <instantee/direction/motion_palette_lint.hpp>
#include <instantee/direction/style_bible.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using instantee::core::LinearRgba;
using instantee::direction::lint_motion_palette;
using instantee::direction::lint_severity_id;
using instantee::direction::LintFinding;
using instantee::direction::LintSeverity;
using instantee::direction::load_style_bible_file;
using instantee::direction::motion_palette_lint_rules_from_style_bible;
using instantee::direction::MotionLintColorUse;
using instantee::direction::MotionLintCut;
using instantee::direction::MotionLintPresetUse;
using instantee::direction::MotionLintSample;
using instantee::direction::MotionPaletteLintInput;
using instantee::direction::MotionPaletteLintRules;
using instantee::direction::palette_color_delta;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
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

[[nodiscard]] bool has_severity(const std::vector<LintFinding> &findings,
                                LintSeverity severity) {
  for (const LintFinding &f : findings) {
    if (f.severity == severity) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] MotionPaletteLintInput calm_input() {
  MotionPaletteLintInput input;
  input.cuts.push_back(
      MotionLintCut{.at_frame = 300, .id = "t0", .path = "/transitions/0"});
  input.cuts.push_back(
      MotionLintCut{.at_frame = 900, .id = "t1", .path = "/transitions/1"});
  input.motion_samples.push_back(
      MotionLintSample{.frame = 0,
                       .element_id = "headline",
                       .path = "/shots/0/nodes/headline",
                       .x_px = 64,
                       .y_px = 200});
  input.motion_samples.push_back(
      MotionLintSample{.frame = 18,
                       .element_id = "headline",
                       .path = "/shots/0/nodes/headline",
                       .x_px = 64,
                       .y_px = 180});
  input.colors.push_back(
      MotionLintColorUse{.id = "ink_fill",
                         .path = "/shots/0/nodes/headline/fill",
                         .color = LinearRgba{0.02, 0.02, 0.03, 1.0}});
  input.presets.push_back(MotionLintPresetUse{.preset_id = "headline_reveal",
                                              .id = "use0",
                                              .path = "/shots/0/motion/0"});
  return input;
}

[[nodiscard]] MotionPaletteLintInput dense_input() {
  MotionPaletteLintInput input;
  for (std::uint64_t i = 0; i < 25; ++i) {
    const std::string idx = (i < 10 ? "0" : "") + std::to_string(i);
    input.cuts.push_back(MotionLintCut{
        .at_frame = i * 72, .id = "c" + idx, .path = "/transitions/" + idx});
  }
  input.cuts.push_back(MotionLintCut{
      .at_frame = 1730, .id = "c_tight", .path = "/transitions/tight"});
  input.motion_samples.push_back(
      MotionLintSample{.frame = 10,
                       .element_id = "badge",
                       .path = "/shots/1/nodes/badge",
                       .x_px = 100,
                       .y_px = 400});
  input.motion_samples.push_back(
      MotionLintSample{.frame = 11,
                       .element_id = "badge",
                       .path = "/shots/1/nodes/badge",
                       .x_px = 420,
                       .y_px = 400});
  input.colors.push_back(
      MotionLintColorUse{.id = "neon",
                         .path = "/shots/1/nodes/badge/fill",
                         .color = LinearRgba{0.0, 0.95, 0.9, 1.0}});
  input.presets.push_back(MotionLintPresetUse{
      .preset_id = "headline_reveal", .id = "p0", .path = "/shots/0/motion/0"});
  input.presets.push_back(MotionLintPresetUse{
      .preset_id = "headline_reveal", .id = "p1", .path = "/shots/1/motion/0"});
  return input;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: motion_palette_lint_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  expect(std::filesystem::exists(
             root / "contracts/fixtures/motion-palette-lint/positive/"
                    "calm-editorial-timeline.json"),
         "positive fixture");
  expect(std::filesystem::exists(
             root / "contracts/fixtures/motion-palette-lint/negative/"
                    "dense-strobe-offpalette.json"),
         "negative fixture");

  const auto editorial_path =
      root / "contracts/fixtures/style-bible/positive/editorial.style.json";
  const auto paper_path =
      root / "contracts/fixtures/style-bible/positive/paper-cinema.style.json";
  auto editorial = load_style_bible_file(editorial_path);
  expect(editorial.has_value(), "load editorial bible");
  auto paper = load_style_bible_file(paper_path);
  expect(paper.has_value(), "load paper bible");
  if (!editorial || !paper) {
    return EXIT_FAILURE;
  }

  auto rules =
      motion_palette_lint_rules_from_style_bible(*editorial, 30.0, 1800);
  expect(rules.style_package_id == "editorial", "rules package_id");
  expect(rules.style_version == "1.0.0", "rules style_version");
  expect(rules.rules_version == "1.0.0", "rules version tracks bible");
  expect(rules.palette.size() == editorial->colors().size(), "palette size");
  expect(palette_color_delta(LinearRgba{0, 0, 0, 1}, LinearRgba{1, 0, 0, 1}) >
             0.9,
         "delta helper");

  // Positive: calm timeline against editorial palette.
  {
    auto findings = lint_motion_palette(calm_input(), rules);
    expect(findings.has_value() && findings->empty(), "calm timeline clean");
  }

  // Negative: density/spacing/strobe/palette/preset with all severities.
  {
    auto findings = lint_motion_palette(dense_input(), rules);
    expect(findings.has_value(), "dense timeline lints");
    if (findings) {
      expect(has_code(*findings, "excessive_cuts"), "excessive cuts");
      expect(has_code(*findings, "cut_spacing"), "cut spacing");
      expect(has_code(*findings, "stroboscopic_motion"), "strobe");
      expect(has_code(*findings, "palette_mismatch"), "palette");
      expect(has_code(*findings, "repeated_preset"), "repeated preset");
      expect(has_severity(*findings, LintSeverity::error), "has error");
      expect(has_severity(*findings, LintSeverity::warning), "has warning");
      expect(has_severity(*findings, LintSeverity::info), "has info");

      for (const LintFinding &f : *findings) {
        expect(!f.code.empty() && !f.path.empty() && !f.element_id.empty(),
               "stable finding fields");
        expect(std::string(lint_severity_id(f.severity)) == "error" ||
                   std::string(lint_severity_id(f.severity)) == "warning" ||
                   std::string(lint_severity_id(f.severity)) == "info",
               "severity id");
      }

      auto again = lint_motion_palette(dense_input(), rules);
      expect(again.has_value() && again->size() == findings->size(),
             "deterministic count");
      if (again && again->size() == findings->size()) {
        for (std::size_t i = 0; i < findings->size(); ++i) {
          expect((*findings)[i].code == (*again)[i].code &&
                     (*findings)[i].path == (*again)[i].path &&
                     (*findings)[i].severity == (*again)[i].severity,
                 "deterministic order");
        }
      }
    }
  }

  // Rules version with Style Bible: same off-palette color against paper may
  // differ; rule identity follows package@version.
  {
    auto paper_rules =
        motion_palette_lint_rules_from_style_bible(*paper, 30.0, 1800);
    expect(paper_rules.style_package_id == "paper-cinema", "paper package");
    expect(paper_rules.rules_version == paper->version(),
           "paper rules_version");
    expect(paper_rules.style_package_id != rules.style_package_id ||
               paper_rules.style_version != rules.style_version,
           "distinct style identity");

    // Bumping rules_version without changing tokens still versions the policy.
    MotionPaletteLintRules revised = rules;
    revised.rules_version = "1.0.1";
    expect(revised.rules_version != rules.rules_version, "rule revision");
    auto a = lint_motion_palette(calm_input(), rules);
    auto b = lint_motion_palette(calm_input(), revised);
    expect(a.has_value() && b.has_value() && a->empty() && b->empty(),
           "version bump keeps calm findings empty with same thresholds");
  }

  // Invalid rules are rejected.
  {
    MotionPaletteLintRules bad = rules;
    bad.palette.clear();
    auto err = lint_motion_palette(calm_input(), bad);
    expect(!err.has_value(), "empty palette rejected");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
