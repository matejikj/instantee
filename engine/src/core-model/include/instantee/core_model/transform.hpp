#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace instantee::core {

// Canvas / world axes follow SAS §8.1: +X right, +Y down, depth in Z with
// camera-space forward = -Z. Matrices use column vectors and column-major
// storage. mul(A, B) = A*B applies B first, then A (local → parent → world).

struct Vec3 {
  double x = 0;
  double y = 0;
  double z = 0;

  [[nodiscard]] friend constexpr bool operator==(Vec3, Vec3) noexcept = default;
};

struct Vec4 {
  double x = 0;
  double y = 0;
  double z = 0;
  double w = 1;

  [[nodiscard]] friend constexpr bool operator==(Vec4, Vec4) noexcept = default;
};

// 3×3 column-major (m[col * 3 + row]). Useful for 2D affine upper-left blocks.
struct Mat3 {
  std::array<double, 9> m{
      1, 0, 0, 0, 1, 0, 0, 0, 1,
  };

  [[nodiscard]] static constexpr Mat3 identity() noexcept { return Mat3{}; }

  [[nodiscard]] constexpr double operator()(std::size_t row,
                                            std::size_t col) const noexcept {
    return m[col * 3U + row];
  }
  [[nodiscard]] constexpr double &operator()(std::size_t row,
                                             std::size_t col) noexcept {
    return m[col * 3U + row];
  }

  [[nodiscard]] friend constexpr bool operator==(Mat3, Mat3) noexcept = default;
};

// 4×4 column-major (m[col * 4 + row]).
struct Mat4 {
  std::array<double, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

  [[nodiscard]] static constexpr Mat4 identity() noexcept { return Mat4{}; }

  [[nodiscard]] constexpr double operator()(std::size_t row,
                                            std::size_t col) const noexcept {
    return m[col * 4U + row];
  }
  [[nodiscard]] constexpr double &operator()(std::size_t row,
                                             std::size_t col) noexcept {
    return m[col * 4U + row];
  }

  [[nodiscard]] friend constexpr bool operator==(Mat4, Mat4) noexcept = default;
};

// Authoring/runtime TRS fields used to build a local matrix. Opacity is not
// part of the matrix. Anchor is intentionally absent: SAS §8.1 keeps anchor as
// a separate layout concept from pivot.
struct LocalTransform {
  Vec3 translate{};
  Vec3 scale{1, 1, 1};
  double rotation_z_rad = 0;
  Vec3 pivot{};
};

[[nodiscard]] constexpr Mat4 translation(Vec3 t) noexcept {
  Mat4 out = Mat4::identity();
  out(0, 3) = t.x;
  out(1, 3) = t.y;
  out(2, 3) = t.z;
  return out;
}

[[nodiscard]] constexpr Mat4 scaling(Vec3 s) noexcept {
  Mat4 out = Mat4::identity();
  out(0, 0) = s.x;
  out(1, 1) = s.y;
  out(2, 2) = s.z;
  return out;
}

// Positive radians rotate from +X toward +Y (canvas: right toward down).
[[nodiscard]] inline Mat4 rotation_z(double radians) noexcept {
  const double c = std::cos(radians);
  const double s = std::sin(radians);
  Mat4 out = Mat4::identity();
  out(0, 0) = c;
  out(1, 0) = s;
  out(0, 1) = -s;
  out(1, 1) = c;
  return out;
}

[[nodiscard]] constexpr Mat4 mul(const Mat4 &a, const Mat4 &b) noexcept {
  Mat4 out{};
  for (std::size_t col = 0; col < 4; ++col) {
    for (std::size_t row = 0; row < 4; ++row) {
      double sum = 0;
      for (std::size_t k = 0; k < 4; ++k) {
        sum += a(row, k) * b(k, col);
      }
      out(row, col) = sum;
    }
  }
  return out;
}

[[nodiscard]] constexpr Mat3 mul(const Mat3 &a, const Mat3 &b) noexcept {
  Mat3 out{};
  for (std::size_t col = 0; col < 3; ++col) {
    for (std::size_t row = 0; row < 3; ++row) {
      double sum = 0;
      for (std::size_t k = 0; k < 3; ++k) {
        sum += a(row, k) * b(k, col);
      }
      out(row, col) = sum;
    }
  }
  return out;
}

[[nodiscard]] constexpr Vec4 mul(const Mat4 &m, Vec4 v) noexcept {
  return Vec4{
      m(0, 0) * v.x + m(0, 1) * v.y + m(0, 2) * v.z + m(0, 3) * v.w,
      m(1, 0) * v.x + m(1, 1) * v.y + m(1, 2) * v.z + m(1, 3) * v.w,
      m(2, 0) * v.x + m(2, 1) * v.y + m(2, 2) * v.z + m(2, 3) * v.w,
      m(3, 0) * v.x + m(3, 1) * v.y + m(3, 2) * v.z + m(3, 3) * v.w,
  };
}

[[nodiscard]] constexpr Vec3 transform_point(const Mat4 &m, Vec3 p) noexcept {
  const Vec4 out = mul(m, Vec4{p.x, p.y, p.z, 1.0});
  return Vec3{out.x, out.y, out.z};
}

[[nodiscard]] constexpr Vec3 transform_vector(const Mat4 &m, Vec3 v) noexcept {
  const Vec4 out = mul(m, Vec4{v.x, v.y, v.z, 0.0});
  return Vec3{out.x, out.y, out.z};
}

// Local matrix: T * Pivot * Rz * S * InvPivot. A point is scaled/rotated about
// pivot, then translated. Matches local → parent multiplication order.
[[nodiscard]] inline Mat4 local_matrix(const LocalTransform &local) noexcept {
  const Mat4 t = translation(local.translate);
  const Mat4 p = translation(local.pivot);
  const Mat4 r = rotation_z(local.rotation_z_rad);
  const Mat4 s = scaling(local.scale);
  const Mat4 inv_p =
      translation(Vec3{-local.pivot.x, -local.pivot.y, -local.pivot.z});
  return mul(t, mul(p, mul(r, mul(s, inv_p))));
}

// Parent-space matrix times child local matrix (column-vector convention).
[[nodiscard]] constexpr Mat4
combine_parent_local(const Mat4 &parent_world,
                     const Mat4 &child_local) noexcept {
  return mul(parent_world, child_local);
}

[[nodiscard]] constexpr Mat3 mat3_from_mat4(const Mat4 &m) noexcept {
  Mat3 out = Mat3::identity();
  out(0, 0) = m(0, 0);
  out(1, 0) = m(1, 0);
  out(2, 0) = m(2, 0);
  out(0, 1) = m(0, 1);
  out(1, 1) = m(1, 1);
  out(2, 1) = m(2, 1);
  out(0, 2) = m(0, 2);
  out(1, 2) = m(1, 2);
  out(2, 2) = m(2, 2);
  return out;
}

[[nodiscard]] inline bool nearly_equal(double a, double b,
                                       double abs_tol = 1e-9,
                                       double rel_tol = 1e-9) noexcept {
  const double diff = std::fabs(a - b);
  if (diff <= abs_tol) {
    return true;
  }
  return diff <= rel_tol * std::max(std::fabs(a), std::fabs(b));
}

[[nodiscard]] inline bool nearly_equal(const Mat4 &a, const Mat4 &b,
                                       double abs_tol = 1e-9,
                                       double rel_tol = 1e-9) noexcept {
  for (std::size_t i = 0; i < a.m.size(); ++i) {
    if (!nearly_equal(a.m[i], b.m[i], abs_tol, rel_tol)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] inline bool nearly_equal(Vec3 a, Vec3 b, double abs_tol = 1e-9,
                                       double rel_tol = 1e-9) noexcept {
  return nearly_equal(a.x, b.x, abs_tol, rel_tol) &&
         nearly_equal(a.y, b.y, abs_tol, rel_tol) &&
         nearly_equal(a.z, b.z, abs_tol, rel_tol);
}

} // namespace instantee::core
