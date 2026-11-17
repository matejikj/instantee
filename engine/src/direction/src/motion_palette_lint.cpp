#include <instantee/direction/motion_palette_lint.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_error(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "motion_palette_lint", message};
}

[[nodiscard]] bool finite_nonneg(double value) noexcept {
  return std::isfinite(value) && value >= 0.0;
}

void push_finding(std::vector<LintFinding> &out, std::string code,
                  LintSeverity severity, std::string path,
                  std::string element_id, std::string message) {
  out.push_back(LintFinding{.code = std::move(code),
                            .severity = severity,
                            .path = std::move(path),
                            .element_id = std::move(element_id),
                            .message = std::move(message)});
}

[[nodiscard]] double cuts_per_minute(std::size_t cut_count, double frame_rate,
                                     std::uint64_t duration_frames) noexcept {
  if (cut_count == 0 || !(frame_rate > 0.0) || duration_frames == 0) {
    return 0.0;
  }
  const double minutes =
      static_cast<double>(duration_frames) / (frame_rate * 60.0);
  if (!(minutes > 0.0)) {
    return 0.0;
  }
  return static_cast<double>(cut_count) / minutes;
}

} // namespace

double palette_color_delta(core::LinearRgba a, core::LinearRgba b) noexcept {
  const double dr = a.r - b.r;
  const double dg = a.g - b.g;
  const double db = a.b - b.b;
  return std::sqrt(dr * dr + dg * dg + db * db);
}

MotionPaletteLintRules motion_palette_lint_rules_from_style_bible(
    const StyleBible &bible, double frame_rate, std::uint64_t duration_frames) {
  MotionPaletteLintRules rules;
  rules.style_package_id = std::string(bible.package_id());
  rules.style_version = std::string(bible.version());
  rules.rules_version = rules.style_version;
  rules.frame_rate = frame_rate;
  rules.duration_frames = duration_frames == 0 ? 1 : duration_frames;
  rules.palette.reserve(bible.colors().size());
  for (const auto &[id, token] : bible.colors()) {
    (void)id;
    rules.palette.push_back(token.value);
  }
  // Stable order for deterministic nearest-color ties in tests.
  std::sort(rules.palette.begin(), rules.palette.end(),
            [](const core::LinearRgba &a, const core::LinearRgba &b) {
              if (a.r != b.r) {
                return a.r < b.r;
              }
              if (a.g != b.g) {
                return a.g < b.g;
              }
              if (a.b != b.b) {
                return a.b < b.b;
              }
              return a.a < b.a;
            });
  return rules;
}

std::expected<std::vector<LintFinding>, diagnostics::Error>
lint_motion_palette(const MotionPaletteLintInput &input,
                    const MotionPaletteLintRules &rules) {
  if (rules.style_package_id.empty() || rules.style_version.empty() ||
      rules.rules_version.empty()) {
    return std::unexpected(make_error(
        "invalid_lint_rules",
        "Motion/palette lint rules require style package id and versions"));
  }
  if (!(std::isfinite(rules.frame_rate) && rules.frame_rate > 0.0) ||
      rules.duration_frames == 0 ||
      !(std::isfinite(rules.max_cuts_per_minute) &&
        rules.max_cuts_per_minute > 0.0) ||
      rules.min_frames_between_cuts == 0 ||
      !(finite_nonneg(rules.max_translation_delta_px) &&
        rules.max_translation_delta_px > 0.0) ||
      !(finite_nonneg(rules.max_palette_delta) &&
        rules.max_palette_delta > 0.0) ||
      rules.max_identical_preset_uses == 0) {
    return std::unexpected(make_error(
        "invalid_lint_rules",
        "Motion/palette lint thresholds must be finite and in range"));
  }
  if (rules.palette.empty()) {
    return std::unexpected(make_error(
        "invalid_lint_rules",
        "Motion/palette lint requires at least one Style Bible color"));
  }
  for (const core::LinearRgba &c : rules.palette) {
    if (!core::finite_linear_rgba(c)) {
      return std::unexpected(
          make_error("invalid_lint_rules", "Palette color is non-finite"));
    }
  }

  for (const MotionLintCut &cut : input.cuts) {
    if (cut.id.empty() || cut.path.empty()) {
      return std::unexpected(make_error("invalid_lint_input",
                                        "Cuts require non-empty id and path"));
    }
  }
  for (const MotionLintSample &sample : input.motion_samples) {
    if (sample.element_id.empty() || sample.path.empty() ||
        !std::isfinite(sample.x_px) || !std::isfinite(sample.y_px)) {
      return std::unexpected(
          make_error("invalid_lint_input",
                     "Motion samples require id/path and finite positions"));
    }
  }
  for (const MotionLintColorUse &use : input.colors) {
    if (use.id.empty() || use.path.empty() ||
        !core::finite_linear_rgba(use.color)) {
      return std::unexpected(
          make_error("invalid_lint_input",
                     "Color uses require id/path and finite linear-rgba"));
    }
  }
  for (const MotionLintPresetUse &use : input.presets) {
    if (use.preset_id.empty() || use.id.empty() || use.path.empty()) {
      return std::unexpected(make_error(
          "invalid_lint_input", "Preset uses require preset_id, id, and path"));
    }
  }

  std::vector<LintFinding> findings;

  // --- Cuts: density and spacing ---
  {
    std::vector<MotionLintCut> cuts = input.cuts;
    std::sort(cuts.begin(), cuts.end(),
              [](const MotionLintCut &a, const MotionLintCut &b) {
                if (a.at_frame != b.at_frame) {
                  return a.at_frame < b.at_frame;
                }
                return a.path < b.path;
              });

    const double rate =
        cuts_per_minute(cuts.size(), rules.frame_rate, rules.duration_frames);
    if (rate >= rules.max_cuts_per_minute * 2.0) {
      push_finding(findings, "excessive_cuts", LintSeverity::error,
                   "/timeline/cuts", "timeline",
                   "Cut density exceeds twice the Style Bible limit");
    } else if (rate > rules.max_cuts_per_minute) {
      push_finding(findings, "excessive_cuts", LintSeverity::warning,
                   "/timeline/cuts", "timeline",
                   "Cut density exceeds the Style Bible limit");
    }

    for (std::size_t i = 1; i < cuts.size(); ++i) {
      const std::uint64_t prev = cuts[i - 1].at_frame;
      const std::uint64_t cur = cuts[i].at_frame;
      if (cur < prev) {
        continue; // sorted; unreachable
      }
      const std::uint64_t gap = cur - prev;
      if (gap < rules.min_frames_between_cuts) {
        push_finding(findings, "cut_spacing", LintSeverity::error, cuts[i].path,
                     cuts[i].id,
                     "Adjacent cuts are closer than the Style Bible minimum");
      }
    }
  }

  // --- Stroboscopic motion: large per-frame translation ---
  {
    std::map<std::string, std::vector<const MotionLintSample *>> by_element;
    for (const MotionLintSample &sample : input.motion_samples) {
      by_element[sample.element_id].push_back(&sample);
    }
    for (auto &[element_id, samples] : by_element) {
      (void)element_id;
      std::sort(samples.begin(), samples.end(),
                [](const MotionLintSample *a, const MotionLintSample *b) {
                  if (a->frame != b->frame) {
                    return a->frame < b->frame;
                  }
                  return a->path < b->path;
                });
      for (std::size_t i = 1; i < samples.size(); ++i) {
        const MotionLintSample &prev = *samples[i - 1];
        const MotionLintSample &cur = *samples[i];
        if (cur.frame <= prev.frame) {
          continue;
        }
        const double dx = cur.x_px - prev.x_px;
        const double dy = cur.y_px - prev.y_px;
        const double delta = std::sqrt(dx * dx + dy * dy);
        const double frames = static_cast<double>(cur.frame - prev.frame);
        const double per_frame = delta / frames;
        if (per_frame > rules.max_translation_delta_px) {
          push_finding(findings, "stroboscopic_motion", LintSeverity::error,
                       cur.path, cur.element_id,
                       "Per-frame translation exceeds the Style Bible limit");
        }
      }
    }
  }

  // --- Palette mismatch ---
  for (const MotionLintColorUse &use : input.colors) {
    double best = palette_color_delta(use.color, rules.palette.front());
    for (std::size_t i = 1; i < rules.palette.size(); ++i) {
      best = std::min(best, palette_color_delta(use.color, rules.palette[i]));
    }
    if (best >= rules.max_palette_delta * 2.0) {
      push_finding(findings, "palette_mismatch", LintSeverity::error, use.path,
                   use.id, "Color is far outside the Style Bible palette");
    } else if (best > rules.max_palette_delta) {
      push_finding(findings, "palette_mismatch", LintSeverity::warning,
                   use.path, use.id,
                   "Color drifts from the Style Bible palette");
    }
  }

  // --- Repeated motion presets ---
  {
    std::map<std::string, std::vector<const MotionLintPresetUse *>> by_preset;
    for (const MotionLintPresetUse &use : input.presets) {
      by_preset[use.preset_id].push_back(&use);
    }
    const std::uint32_t limit = rules.max_identical_preset_uses;
    const std::uint32_t info_at = (limit + 1) / 2; // ceil(limit/2)
    for (const auto &[preset_id, uses] : by_preset) {
      const auto count = static_cast<std::uint32_t>(uses.size());
      if (count < info_at) {
        continue;
      }
      // Attach the finding to the last use (stable after path sort).
      std::vector<const MotionLintPresetUse *> ordered = uses;
      std::sort(ordered.begin(), ordered.end(),
                [](const MotionLintPresetUse *a, const MotionLintPresetUse *b) {
                  if (a->path != b->path) {
                    return a->path < b->path;
                  }
                  return a->id < b->id;
                });
      const MotionLintPresetUse &anchor = *ordered.back();
      if (count >= limit * 2) {
        push_finding(findings, "repeated_preset", LintSeverity::error,
                     anchor.path, anchor.id,
                     "Motion preset '" + preset_id +
                         "' is overused relative to the Style Bible limit");
      } else if (count >= limit) {
        push_finding(findings, "repeated_preset", LintSeverity::warning,
                     anchor.path, anchor.id,
                     "Motion preset '" + preset_id +
                         "' reaches the Style Bible reuse limit");
      } else {
        push_finding(findings, "repeated_preset", LintSeverity::info,
                     anchor.path, anchor.id,
                     "Motion preset '" + preset_id +
                         "' is approaching the Style Bible reuse limit");
      }
    }
  }

  std::sort(findings.begin(), findings.end(),
            [](const LintFinding &a, const LintFinding &b) {
              if (a.path != b.path) {
                return a.path < b.path;
              }
              if (a.code != b.code) {
                return a.code < b.code;
              }
              if (a.element_id != b.element_id) {
                return a.element_id < b.element_id;
              }
              if (a.severity != b.severity) {
                return static_cast<std::uint8_t>(a.severity) <
                       static_cast<std::uint8_t>(b.severity);
              }
              return a.message < b.message;
            });
  return findings;
}

} // namespace instantee::direction
