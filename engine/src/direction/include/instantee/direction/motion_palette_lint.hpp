#pragma once

#include <instantee/core_model/color.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/lint_types.hpp>
#include <instantee/direction/style_bible.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Versioned motion/palette policy. Defaults are derived from a Style Bible so
// thresholds and palette travel with package_id@version (SAS §9.5). Aesthetic
// quality is not asserted as absolute truth.
struct MotionPaletteLintRules {
  std::string style_package_id;
  std::string style_version;
  // Distinct from style_version only when a style ships rule revisions without
  // changing token content; defaults to style_version.
  std::string rules_version;

  double frame_rate = 30.0;
  std::uint64_t duration_frames = 1;

  // Cut density: warning above max_cuts_per_minute, error at/above 2×.
  double max_cuts_per_minute = 24.0;
  // Adjacent cuts closer than this yield an error.
  std::uint32_t min_frames_between_cuts = 6;

  // Frame-to-frame translation above this (same element) is stroboscopic.
  double max_translation_delta_px = 160.0;

  // Euclidean distance in linear RGB to nearest Style Bible color.
  // Warning above threshold; error at/above 2×.
  double max_palette_delta = 0.18;

  // Identical motion preset id uses: info at half the limit (rounded up),
  // warning at the limit, error at/above 2×.
  std::uint32_t max_identical_preset_uses = 4;

  std::vector<core::LinearRgba> palette;
};

struct MotionLintCut {
  std::uint64_t at_frame = 0;
  std::string id;
  std::string path;
};

// Position samples for one element across frames. Callers supply the samples;
// the linter does not evaluate tracks.
struct MotionLintSample {
  std::uint64_t frame = 0;
  std::string element_id;
  std::string path;
  double x_px = 0;
  double y_px = 0;
};

struct MotionLintColorUse {
  std::string id;
  std::string path;
  core::LinearRgba color{};
};

struct MotionLintPresetUse {
  std::string preset_id;
  std::string id;
  std::string path;
};

struct MotionPaletteLintInput {
  std::vector<MotionLintCut> cuts;
  std::vector<MotionLintSample> motion_samples;
  std::vector<MotionLintColorUse> colors;
  std::vector<MotionLintPresetUse> presets;
};

// Build rules + palette from a loaded Style Bible. Changing the bible version
// or color tokens changes the effective lint policy without touching
// core-model.
[[nodiscard]] MotionPaletteLintRules motion_palette_lint_rules_from_style_bible(
    const StyleBible &bible, double frame_rate = 30.0,
    std::uint64_t duration_frames = 1800);

[[nodiscard]] std::expected<std::vector<LintFinding>, diagnostics::Error>
lint_motion_palette(const MotionPaletteLintInput &input,
                    const MotionPaletteLintRules &rules);

// Linear-RGB Euclidean distance (alpha ignored).
[[nodiscard]] double palette_color_delta(core::LinearRgba a,
                                         core::LinearRgba b) noexcept;

} // namespace instantee::direction
