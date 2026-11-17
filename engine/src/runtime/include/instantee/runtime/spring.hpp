#pragma once

#include <instantee/core_model/time.hpp>
#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>

namespace instantee::runtime {

// Fixed-parameter damped harmonic oscillator. Sampling is a pure function of
// absolute time (or frame + rate); never of a previous rendered frame.
struct SpringPreset {
  double angular_frequency = 1.0; // ω0 > 0
  double damping_ratio = 1.0;     // ζ ≥ 0
};

enum class SpringRegime : std::uint8_t {
  underdamped = 0,
  critically_damped,
  overdamped
};

[[nodiscard]] std::expected<SpringPreset, diagnostics::Error>
make_spring_preset(double angular_frequency, double damping_ratio);

[[nodiscard]] SpringRegime spring_regime(const SpringPreset &preset) noexcept;

// Displacement from `from` toward equilibrium `to` with initial velocity
// (value units per second). `seconds` < 0 is rejected. Result is finite for
// valid presets; large times approach `to`.
[[nodiscard]] std::expected<double, diagnostics::Error>
sample_spring(const SpringPreset &preset, double from, double to,
              double initial_velocity, double seconds) noexcept;

// Same curve at the start PTS of `frame` under `rate`
// (t = frame * denominator / numerator seconds).
[[nodiscard]] std::expected<double, diagnostics::Error>
sample_spring_at_frame(const SpringPreset &preset, double from, double to,
                       double initial_velocity, core::FrameIndex frame,
                       core::FrameRate rate) noexcept;

} // namespace instantee::runtime
