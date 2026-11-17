#include <instantee/core_model/orientation.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace instantee::core {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "core_model", message};
}

bool finite_quat(Quat q) noexcept {
  return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) &&
         std::isfinite(q.w);
}

} // namespace

std::expected<Quat, diagnostics::Error> quat_normalize(Quat q) {
  if (!finite_quat(q)) {
    return std::unexpected(
        err("non_finite_quat", "Quaternion components must be finite"));
  }
  const double len_sq = quat_dot(q, q);
  if (!(len_sq > 0.0)) {
    return std::unexpected(
        err("zero_quat", "Cannot normalize a zero-length quaternion"));
  }
  const double inv = 1.0 / std::sqrt(len_sq);
  return Quat{q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

std::expected<Quat, diagnostics::Error> slerp_quat_short(Quat from, Quat to,
                                                         double t) {
  if (!std::isfinite(t)) {
    return std::unexpected(
        err("non_finite_t", "Interpolation parameter must be finite"));
  }
  auto a = quat_normalize(from);
  if (!a) {
    return std::unexpected(a.error());
  }
  auto b = quat_normalize(to);
  if (!b) {
    return std::unexpected(b.error());
  }

  if (t <= 0.0) {
    return *a;
  }
  if (t >= 1.0) {
    // Prefer the short-arc endpoint orientation (may equal -*b as a rotation).
    double dot = quat_dot(*a, *b);
    if (dot < 0.0) {
      return quat_negate(*b);
    }
    return *b;
  }

  double dot = quat_dot(*a, *b);
  Quat b_arc = *b;
  if (dot < 0.0) {
    b_arc = quat_negate(b_arc);
    dot = -dot;
  }
  // Clamp numerical drift into the acos domain.
  dot = std::clamp(dot, -1.0, 1.0);

  constexpr double kLinearThreshold = 0.9995;
  if (dot > kLinearThreshold) {
    Quat lerped{a->x + t * (b_arc.x - a->x), a->y + t * (b_arc.y - a->y),
                a->z + t * (b_arc.z - a->z), a->w + t * (b_arc.w - a->w)};
    return quat_normalize(lerped);
  }

  const double omega = std::acos(dot);
  const double sin_omega = std::sin(omega);
  if (!(std::abs(sin_omega) > 0.0)) {
    return *a;
  }
  const double w0 = std::sin((1.0 - t) * omega) / sin_omega;
  const double w1 = std::sin(t * omega) / sin_omega;
  Quat out{w0 * a->x + w1 * b_arc.x, w0 * a->y + w1 * b_arc.y,
           w0 * a->z + w1 * b_arc.z, w0 * a->w + w1 * b_arc.w};
  return quat_normalize(out);
}

} // namespace instantee::core
