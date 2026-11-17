#include <instantee/runtime/spring.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

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
  using namespace instantee::runtime;

  expect(!make_spring_preset(0.0, 1.0).has_value(), "reject omega0 == 0");
  expect(!make_spring_preset(-1.0, 1.0).has_value(), "reject omega0 < 0");
  expect(!make_spring_preset(1.0, -0.1).has_value(), "reject zeta < 0");

  auto under = make_spring_preset(8.0, 0.4);
  auto critical = make_spring_preset(8.0, 1.0);
  auto over = make_spring_preset(8.0, 2.5);
  expect(under && critical && over, "valid presets");
  if (!(under && critical && over)) {
    return EXIT_FAILURE;
  }
  expect(spring_regime(*under) == SpringRegime::underdamped, "under regime");
  expect(spring_regime(*critical) == SpringRegime::critically_damped,
         "critical regime");
  expect(spring_regime(*over) == SpringRegime::overdamped, "over regime");

  // t = 0 is exactly the start value for every regime.
  for (const SpringPreset &preset : {*under, *critical, *over}) {
    const auto at0 = sample_spring(preset, 0.0, 1.0, 0.0, 0.0);
    expect(at0.has_value(), "sample at t=0");
    if (at0) {
      expect_near(*at0, 0.0, 0.0, "t=0 returns from");
    }
  }

  // Large time settles near the equilibrium (from rest).
  for (const SpringPreset &preset : {*under, *critical, *over}) {
    const auto settled = sample_spring(preset, 0.0, 1.0, 0.0, 20.0);
    expect(settled.has_value() && std::isfinite(*settled), "settled finite");
    if (settled) {
      expect_near(*settled, 1.0, 1e-6, "settles to target");
    }
  }

  // Overdamped from rest never crosses the target.
  {
    double prev = 0.0;
    bool crossed = false;
    for (int i = 0; i <= 200; ++i) {
      const double t = static_cast<double>(i) * 0.01;
      const auto y = sample_spring(*over, 0.0, 1.0, 0.0, t);
      expect(y.has_value(), "overdamped sample");
      if (!y) {
        break;
      }
      if (*y > 1.0 + 1e-9) {
        crossed = true;
      }
      expect(*y + 1e-12 >= prev, "overdamped monotone from rest");
      prev = *y;
    }
    expect(!crossed, "overdamped does not overshoot from rest");
  }

  // Underdamped from rest does overshoot at least once.
  {
    bool overshoot = false;
    for (int i = 1; i <= 200; ++i) {
      const double t = static_cast<double>(i) * 0.01;
      const auto y = sample_spring(*under, 0.0, 1.0, 0.0, t);
      if (y && *y > 1.0 + 1e-6) {
        overshoot = true;
        break;
      }
    }
    expect(overshoot, "underdamped overshoots from rest");
  }

  // Random-access order independence: permute frame samples, compare bit-equal
  // values to a reference pass in ascending order.
  {
    const FrameRate rate = FrameRate::media_30_1();
    const std::vector<std::uint64_t> order = {17, 0, 5, 30, 2, 11, 9};
    std::vector<double> reference(31);
    for (std::uint64_t f = 0; f <= 30; ++f) {
      const auto y =
          sample_spring_at_frame(*under, 2.0, 5.0, 0.0, FrameIndex{f}, rate);
      expect(y.has_value(), "reference frame sample");
      if (y) {
        reference[static_cast<std::size_t>(f)] = *y;
      }
    }
    for (std::uint64_t f : order) {
      const auto y =
          sample_spring_at_frame(*under, 2.0, 5.0, 0.0, FrameIndex{f}, rate);
      expect(y.has_value(), "permuted frame sample");
      if (y) {
        // Bit-identical for the same inputs regardless of call order.
        expect(std::memcmp(&*y, &reference[static_cast<std::size_t>(f)],
                           sizeof(double)) == 0,
               "frame N independent of evaluation order");
      }
    }
  }

  // Critical near-ζ=1 classification stays critical.
  auto near = make_spring_preset(5.0, 1.0 + 1e-12);
  expect(near.has_value() &&
             spring_regime(*near) == SpringRegime::critically_damped,
         "near-critical classified as critical");

  expect(!sample_spring(*under, 0.0, 1.0, 0.0, -0.1).has_value(),
         "reject negative time");
  expect(!sample_spring(*under, std::numeric_limits<double>::quiet_NaN(), 1.0,
                        0.0, 0.0)
              .has_value(),
         "reject NaN from");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
