#include <instantee/core_model/color.hpp>
#include <instantee/core_model/orientation.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void expect_near(double actual, double expected, double tol,
                 const char *message) {
  if (!(std::isfinite(actual) && std::abs(actual - expected) <= tol)) {
    std::cerr << "FAIL: " << message << " got " << actual << " expected "
              << expected << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  using namespace instantee::core;
  constexpr double kPi = std::numbers::pi_v<double>;

  // Short arc across the ±π cut: from near +π toward near −π goes the short
  // way.
  {
    const double from = kPi - 0.1;
    const double to = -kPi + 0.1;
    const double delta = shortest_signed_angle_delta(from, to);
    expect(delta > 0.0, "short delta across +π is positive");
    expect_near(delta, 0.2, 1e-12, "short delta magnitude 0.2");
    const double mid = lerp_angle_short(from, to, 0.5);
    expect_near(wrap_angle_pi(mid), wrap_angle_pi(kPi), 1e-9,
                "midpoint near ±π cut");
    expect_near(lerp_angle_short(from, to, 0.0), from, 0.0, "angle t=0");
    expect_near(lerp_angle_short(from, to, 1.0), to, 0.0, "angle t=1");
  }

  // Opposite direction across −π → +π.
  {
    const double from = -kPi + 0.1;
    const double to = kPi - 0.1;
    const double delta = shortest_signed_angle_delta(from, to);
    expect(delta < 0.0, "short delta across -π is negative");
    expect_near(delta, -0.2, 1e-12, "short delta magnitude -0.2");
  }

  // Exact ±π equivalence: wrap(+π) and wrap(−π) land on the same cut.
  expect_near(wrap_angle_pi(kPi), wrap_angle_pi(-kPi), 1e-15,
              "±π wrap to same principal cut");

  // Quaternion short-arc slerp: opposite hemispheres take the short path.
  {
    Quat a = quat_identity();
    // 180° about Z would be (0,0,1,0); use 90° about Z: (0,0,sin45,cos45).
    const double s = std::sin(kPi / 4.0);
    const double c = std::cos(kPi / 4.0);
    Quat b{0.0, 0.0, s, c};
    auto mid = slerp_quat_short(a, b, 0.5);
    expect(mid.has_value(), "slerp mid");
    if (mid) {
      expect_near(quat_dot(*mid, *mid), 1.0, 1e-12, "mid unit length");
      // Midway 45° about Z.
      const double s2 = std::sin(kPi / 8.0);
      const double c2 = std::cos(kPi / 8.0);
      expect_near(mid->z, s2, 1e-9, "slerp z");
      expect_near(mid->w, c2, 1e-9, "slerp w");
    }

    // Negated endpoint represents the same rotation; short arc still matches.
    auto mid_neg = slerp_quat_short(a, quat_negate(b), 0.5);
    expect(mid_neg.has_value() && mid.has_value(), "slerp negated endpoint");
    if (mid && mid_neg) {
      expect_near(std::abs(quat_dot(*mid, *mid_neg)), 1.0, 1e-9,
                  "short path agrees up to sign");
    }

    expect(!quat_normalize(Quat{0, 0, 0, 0}).has_value(), "reject zero quat");
  }

  // Linear working-space color: midpoint of linear black→white is 0.5 linear,
  // which is not 0.5 in sRGB.
  {
    const LinearRgba black = srgb_rgba_to_linear(0.0, 0.0, 0.0, 1.0);
    const LinearRgba white = srgb_rgba_to_linear(1.0, 1.0, 1.0, 1.0);
    auto mid = lerp_linear_rgba(black, white, 0.5);
    expect(mid.has_value(), "color mid");
    if (mid) {
      expect_near(mid->r, 0.5, 1e-12, "linear mid red");
      expect_near(mid->g, 0.5, 1e-12, "linear mid green");
      expect_near(mid->b, 0.5, 1e-12, "linear mid blue");
      const double srgb_mid = linear_channel_to_srgb(mid->r);
      expect(srgb_mid > 0.7 && srgb_mid < 0.8, "sRGB encode of 0.5 linear");
    }

    // Direct sRGB-channel average would be wrong for perception/light; our
    // contract requires linear lerp after decode.
    const double naive_srgb_avg = 0.5;
    const double linear_of_naive = srgb_channel_to_linear(naive_srgb_avg);
    expect(std::abs(linear_of_naive - 0.5) > 0.1,
           "sRGB 0.5 is not 0.5 linear (contract distinction)");
  }

  // Alpha mixes in the same linear coverage space.
  {
    auto faded =
        lerp_linear_rgba(LinearRgba{1, 0, 0, 1}, LinearRgba{1, 0, 0, 0}, 0.25);
    expect(faded.has_value(), "alpha lerp");
    if (faded) {
      expect_near(faded->a, 0.75, 1e-12, "alpha mid");
      expect_near(faded->r, 1.0, 1e-12, "rgb unchanged");
    }
  }

  expect(!lerp_linear_rgba(LinearRgba{0, 0, 0, 1}, LinearRgba{1, 1, 1, 1},
                           std::numeric_limits<double>::quiet_NaN())
              .has_value(),
         "reject NaN t");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
