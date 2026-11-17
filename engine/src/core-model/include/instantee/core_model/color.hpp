#pragma once

#include <instantee/diagnostics/error.hpp>

#include <algorithm>
#include <cmath>
#include <expected>

namespace instantee::core {

// Working-space color for animation: linear light RGB + linear alpha coverage.
// Do not lerp authored sRGB channels directly; convert with
// srgb_channel_to_linear first, interpolate here, then encode back if needed.

struct LinearRgba {
  double r = 0;
  double g = 0;
  double b = 0;
  double a = 1;

  [[nodiscard]] friend constexpr bool operator==(LinearRgba,
                                                 LinearRgba) noexcept = default;
};

[[nodiscard]] inline bool finite_linear_rgba(LinearRgba c) noexcept {
  return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) &&
         std::isfinite(c.a);
}

// IEC 61966-2-1 transfer for a single channel in [0, 1] (clamped).
[[nodiscard]] inline double srgb_channel_to_linear(double srgb) noexcept {
  const double u = std::clamp(srgb, 0.0, 1.0);
  if (u <= 0.04045) {
    return u / 12.92;
  }
  return std::pow((u + 0.055) / 1.055, 2.4);
}

[[nodiscard]] inline double linear_channel_to_srgb(double linear) noexcept {
  const double u = std::clamp(linear, 0.0, 1.0);
  if (u <= 0.0031308) {
    return 12.92 * u;
  }
  return 1.055 * std::pow(u, 1.0 / 2.4) - 0.055;
}

[[nodiscard]] inline LinearRgba
srgb_rgba_to_linear(double r, double g, double b, double a) noexcept {
  return LinearRgba{srgb_channel_to_linear(r), srgb_channel_to_linear(g),
                    srgb_channel_to_linear(b), std::clamp(a, 0.0, 1.0)};
}

// Component-wise lerp in linear working space. t is clamped to [0, 1].
[[nodiscard]] std::expected<LinearRgba, diagnostics::Error>
lerp_linear_rgba(LinearRgba from, LinearRgba to, double t);

} // namespace instantee::core
