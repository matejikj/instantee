#include <instantee/direction/creative_variation.hpp>

#include <instantee/core_model/deterministic_rng.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace instantee::direction {
namespace {

diagnostics::Error make_error(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "creative_variation", message};
}

[[nodiscard]] bool finite_interval(double lo, double hi) noexcept {
  return std::isfinite(lo) && std::isfinite(hi) && lo <= hi;
}

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_limits(const VariationSafeIntervals &limits) {
  if (!finite_interval(limits.intensity_min, limits.intensity_max) ||
      limits.intensity_min <= 0.0) {
    return std::unexpected(make_error(
        "invalid_safe_interval",
        "Intensity safe interval must be finite with 0 < min <= max"));
  }
  if (!finite_interval(limits.duration_scale_min, limits.duration_scale_max) ||
      limits.duration_scale_min <= 0.0) {
    return std::unexpected(make_error(
        "invalid_safe_interval",
        "Duration-scale safe interval must be finite with 0 < min <= max"));
  }
  if (!finite_interval(limits.stagger_scale_min, limits.stagger_scale_max) ||
      limits.stagger_scale_min < 0.0) {
    return std::unexpected(make_error(
        "invalid_safe_interval",
        "Stagger-scale safe interval must be finite with 0 <= min <= max"));
  }
  return {};
}

[[nodiscard]] std::expected<void, diagnostics::Error>
require_nonempty(std::string_view value, std::string_view code,
                 std::string_view message) {
  if (value.empty()) {
    return std::unexpected(make_error(code, message));
  }
  return {};
}

} // namespace

std::expected<std::uint64_t, diagnostics::Error>
derive_shot_subseed(std::uint64_t film_seed, std::string_view package_id,
                    std::string_view package_version,
                    std::string_view shot_id) {
  if (auto ok = require_nonempty(package_id, "invalid_package_id",
                                 "Style package_id is required");
      !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = require_nonempty(package_version, "invalid_package_version",
                                 "Style package version is required");
      !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = require_nonempty(shot_id, "invalid_shot_id",
                                 "Shot id is required for sub-seed derivation");
      !ok) {
    return std::unexpected(ok.error());
  }

  // Style Bible version participates so a package revision changes variation
  // space intentionally; unrelated shots never enter this mix.
  std::uint64_t h = core::splitmix64(film_seed);
  h = core::splitmix64(h ^ core::fnv1a64(package_id));
  h = core::splitmix64(h ^ core::fnv1a64(package_version));
  h = core::splitmix64(h ^ core::fnv1a64("shot"));
  h = core::splitmix64(h ^ core::fnv1a64(shot_id));
  return h;
}

std::expected<std::uint64_t, diagnostics::Error>
derive_object_subseed(std::uint64_t shot_subseed, std::string_view object_id) {
  if (auto ok = require_nonempty(object_id, "invalid_object_id",
                                 "Object id is required for object sub-seed");
      !ok) {
    return std::unexpected(ok.error());
  }
  std::uint64_t h = core::splitmix64(shot_subseed);
  h = core::splitmix64(h ^ core::fnv1a64("object"));
  h = core::splitmix64(h ^ core::fnv1a64(object_id));
  return h;
}

std::expected<double, diagnostics::Error>
sample_variation_unit(std::uint64_t object_subseed, std::string_view channel,
                      double lo, double hi) {
  if (auto ok = require_nonempty(channel, "invalid_variation_channel",
                                 "Variation channel name is required");
      !ok) {
    return std::unexpected(ok.error());
  }
  if (!finite_interval(lo, hi)) {
    return std::unexpected(
        make_error("invalid_safe_interval",
                   "Variation sample interval must be finite with min <= max"));
  }
  const std::uint64_t mixed =
      core::splitmix64(object_subseed ^ core::fnv1a64(channel));
  const double unit = core::hash_to_unit(mixed);
  if (!(unit >= 0.0 && unit < 1.0)) {
    return std::unexpected(make_error("variation_sample_out_of_range",
                                      "Unit variation sample escaped [0, 1)"));
  }
  if (lo == hi) {
    return lo;
  }
  return lo + (hi - lo) * unit;
}

std::expected<CreativeVariation, diagnostics::Error>
sample_creative_variation(const VariationIdentity &identity,
                          const VariationSafeIntervals &limits) {
  if (auto ok = validate_limits(limits); !ok) {
    return std::unexpected(ok.error());
  }
  auto shot = derive_shot_subseed(identity.film_seed, identity.package_id,
                                  identity.package_version, identity.shot_id);
  if (!shot) {
    return std::unexpected(shot.error());
  }
  auto object = derive_object_subseed(*shot, identity.object_id);
  if (!object) {
    return std::unexpected(object.error());
  }

  CreativeVariation out;
  out.shot_subseed = *shot;
  out.object_subseed = *object;

  auto intensity = sample_variation_unit(
      *object, "intensity_scale", limits.intensity_min, limits.intensity_max);
  if (!intensity) {
    return std::unexpected(intensity.error());
  }
  auto duration = sample_variation_unit(*object, "duration_scale",
                                        limits.duration_scale_min,
                                        limits.duration_scale_max);
  if (!duration) {
    return std::unexpected(duration.error());
  }
  auto stagger =
      sample_variation_unit(*object, "stagger_scale", limits.stagger_scale_min,
                            limits.stagger_scale_max);
  if (!stagger) {
    return std::unexpected(stagger.error());
  }

  out.intensity_scale = *intensity;
  out.duration_scale = *duration;
  out.stagger_scale = *stagger;
  return out;
}

std::expected<MotionGrammarBinding, diagnostics::Error>
apply_creative_variation(const MotionGrammarBinding &binding,
                         const CreativeVariation &variation) {
  if (!std::isfinite(variation.intensity_scale) ||
      !std::isfinite(variation.duration_scale) ||
      !std::isfinite(variation.stagger_scale) ||
      variation.intensity_scale <= 0.0 || variation.duration_scale <= 0.0 ||
      variation.stagger_scale < 0.0) {
    return std::unexpected(make_error(
        "invalid_variation",
        "Creative variation multipliers must be finite and within safe signs"));
  }

  MotionGrammarBinding out = binding;
  out.intensity_scale = binding.intensity_scale * variation.intensity_scale;
  out.duration_scale =
      std::clamp(binding.duration_scale * variation.duration_scale, 0.05, 1.0);

  const double scaled_stagger =
      static_cast<double>(binding.stagger_frames) * variation.stagger_scale;
  if (!std::isfinite(scaled_stagger) ||
      scaled_stagger >
          static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
    return std::unexpected(
        make_error("stagger_overflow", "Varied stagger_frames overflow"));
  }
  out.stagger_frames = static_cast<std::uint32_t>(scaled_stagger);
  return out;
}

} // namespace instantee::direction
