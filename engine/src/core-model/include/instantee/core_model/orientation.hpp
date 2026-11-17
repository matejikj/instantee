#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cmath>
#include <expected>

namespace instantee::core {

// 2D angles are radians. Short-arc policy picks the signed delta in (-π, π]
// so interpolation never takes the long way across the ±π cut.

[[nodiscard]] inline double wrap_angle_pi(double radians) noexcept {
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kTwoPi = 2.0 * kPi;
  double x = std::fmod(radians + kPi, kTwoPi);
  if (x < 0.0) {
    x += kTwoPi;
  }
  return x - kPi;
}

[[nodiscard]] inline double
shortest_signed_angle_delta(double from_rad, double to_rad) noexcept {
  return wrap_angle_pi(to_rad - from_rad);
}

// t is clamped to [0, 1]. The result is not re-wrapped so multi-turn paths
// stay continuous when composed; call wrap_angle_pi at a publish boundary if
// a principal value in (-π, π] is required.
[[nodiscard]] inline double lerp_angle_short(double from_rad, double to_rad,
                                             double t) noexcept {
  if (t <= 0.0) {
    return from_rad;
  }
  if (t >= 1.0) {
    return to_rad;
  }
  return from_rad + shortest_signed_angle_delta(from_rad, to_rad) * t;
}

// Unit quaternion, Hamilton product, scalar-last storage (x, y, z, w).
// Short-arc slerp flips the sign of `to` when dot < 0 so the path is ≤ 180°.
struct Quat {
  double x = 0;
  double y = 0;
  double z = 0;
  double w = 1;

  [[nodiscard]] friend constexpr bool operator==(Quat, Quat) noexcept = default;
};

[[nodiscard]] inline Quat quat_identity() noexcept { return Quat{}; }

[[nodiscard]] inline double quat_dot(Quat a, Quat b) noexcept {
  return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

[[nodiscard]] inline Quat quat_negate(Quat q) noexcept {
  return Quat{-q.x, -q.y, -q.z, -q.w};
}

[[nodiscard]] std::expected<Quat, diagnostics::Error> quat_normalize(Quat q);

// Spherical linear interpolation along the short arc. Near-parallel inputs
// fall back to normalized lerp to avoid division by a vanishing sin(Ω).
[[nodiscard]] std::expected<Quat, diagnostics::Error>
slerp_quat_short(Quat from, Quat to, double t);

} // namespace instantee::core
