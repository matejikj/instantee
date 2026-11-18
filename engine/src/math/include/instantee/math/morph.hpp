#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/math/semantic.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::math {

// How a correspondence pair participates in the morph. Persist keeps one
// identity across both scenes; enter/exit cover unmatched arrivals/departures.
enum class MathMorphRole : std::uint8_t {
  persist = 1,
  enter,
  exit,
};

// Named unit easings for morph progress. Kept local to math so the domain
// never depends on the runtime easing backend; curves match common CSS names.
enum class MathMorphEase : std::uint8_t {
  linear = 1,
  ease_in_out,
};

// One 1:1 correspondence entry. Persist requires identical glyph_text when
// either side carries a leaf payload — different glyphs become exit+enter so
// tokens never blend into broken strings.
struct MathMorphPair {
  MathMorphRole role = MathMorphRole::persist;
  std::string correspondence_key{};
  std::string from_id{}; // empty for enter
  std::string to_id{};   // empty for exit
};

struct MathMorphPlan {
  std::vector<MathMorphPair> pairs{};
};

// Sampled paint token at a unit time. Glyph text is never interpolated.
struct MathMorphToken {
  MathMorphRole role = MathMorphRole::persist;
  std::string correspondence_key{};
  std::string owner_id{}; // from_id or to_id depending on role/phase
  std::string glyph_text{};
  double x_px = 0;
  double baseline_y_px = 0;
  double width_px = 0;
  double height_px = 0;
  double depth_px = 0;
  double opacity = 1.0; // clamped to [0, 1]
};

// Map unit time u ∈ [0, 1] through the selected easing. Endpoints stay exact.
[[nodiscard]] double sample_math_morph_ease(MathMorphEase ease,
                                            double unit_time) noexcept;

// Build a stable correspondence plan from two compiled scenes. Matching keys
// are structural (path with leaf values stripped) so equivalent slots align
// across rewrites; unmatched nodes become enter/exit fallbacks.
[[nodiscard]] std::expected<MathMorphPlan, diagnostics::Error>
plan_math_morph(const MathAnimScene &from, const MathAnimScene &to);

// Evaluate the plan at unit time. Result is temporally stable for identical
// inputs. Only leaf tokens with non-empty glyph_text are emitted (structure
// boxes drive matching but are not painted as tokens).
[[nodiscard]] std::expected<std::vector<MathMorphToken>, diagnostics::Error>
sample_math_morph(const MathMorphPlan &plan, const MathAnimScene &from,
                  const MathAnimScene &to, double unit_time,
                  MathMorphEase ease = MathMorphEase::ease_in_out);

// Deterministic text dump for morph goldens (plan + samples at fixed times).
[[nodiscard]] std::string
format_math_morph_golden(std::string_view label, const MathMorphPlan &plan,
                         const std::vector<MathMorphToken> &at0,
                         const std::vector<MathMorphToken> &at_mid,
                         const std::vector<MathMorphToken> &at1);

} // namespace instantee::math
