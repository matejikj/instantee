#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/motion_grammar.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace instantee::direction {

// Closed safe intervals for CDS creative jitter. Values outside these ranges
// are rejected — variation never invents unconstrained randomness.
struct VariationSafeIntervals {
  double intensity_min = 0.85;
  double intensity_max = 1.15;
  double duration_scale_min = 0.85;
  double duration_scale_max = 1.15;
  double stagger_scale_min = 0.0;
  double stagger_scale_max = 1.0;
};

struct VariationIdentity {
  std::uint64_t film_seed = 0;
  std::string package_id;
  std::string package_version;
  std::string shot_id;
  std::string object_id;
};

struct CreativeVariation {
  std::uint64_t shot_subseed = 0;
  std::uint64_t object_subseed = 0;
  // Multipliers applied to grammar binding scales; always inside safe
  // intervals.
  double intensity_scale = 1.0;
  double duration_scale = 1.0;
  // Extra stagger frames derived from stagger_scale * binding.stagger_frames.
  double stagger_scale = 0.0;
};

// Derive a shot-local sub-seed from the film seed, Style Bible identity, and
// stable shot id. Independent of shot iteration order or sibling shots.
[[nodiscard]] std::expected<std::uint64_t, diagnostics::Error>
derive_shot_subseed(std::uint64_t film_seed, std::string_view package_id,
                    std::string_view package_version, std::string_view shot_id);

// Object stream keyed by shot sub-seed + stable object id (not graph index).
[[nodiscard]] std::expected<std::uint64_t, diagnostics::Error>
derive_object_subseed(std::uint64_t shot_subseed, std::string_view object_id);

// Sample one named channel into [lo, hi] from an object sub-seed. Channel names
// partition the stream so unrelated properties do not collide.
[[nodiscard]] std::expected<double, diagnostics::Error>
sample_variation_unit(std::uint64_t object_subseed, std::string_view channel,
                      double lo, double hi);

// Full creative draw for one object inside a shot. Deterministic for the same
// identity; adding an unrelated shot/object does not change this result.
[[nodiscard]] std::expected<CreativeVariation, diagnostics::Error>
sample_creative_variation(const VariationIdentity &identity,
                          const VariationSafeIntervals &limits = {});

// Apply sampled multipliers to a motion-grammar binding. Stagger frames are
// scaled and clamped; intensity/duration scales stay inside safe intervals.
[[nodiscard]] std::expected<MotionGrammarBinding, diagnostics::Error>
apply_creative_variation(const MotionGrammarBinding &binding,
                         const CreativeVariation &variation);

[[nodiscard]] constexpr VariationSafeIntervals
default_variation_safe_intervals() noexcept {
  return {};
}

} // namespace instantee::direction
