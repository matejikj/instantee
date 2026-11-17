#include <instantee/core_model/deterministic_rng.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
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

  // Fixed golden for the documented mixer (guards silent algorithm drift).
  expect(splitmix64(0) == 0xE220A8397B1DCDAFULL, "splitmix64(0) golden");
  expect(fnv1a64("") == 14695981039346656037ULL, "fnv1a empty");
  expect(fnv1a64("a") != fnv1a64("b"), "fnv1a distinguishes bytes");

  constexpr std::uint64_t kSeed = 0xC0FFEEULL;
  const char *node_a = "headline";
  const char *node_b = "unrelated_deco";
  const char *prop = "opacity";

  // Unit noise stays in [0, 1).
  for (std::uint64_t frame = 0; frame < 64; ++frame) {
    const double u = sample_unit_noise(kSeed, node_a, prop, frame);
    expect(std::isfinite(u) && u >= 0.0 && u < 1.0, "unit noise range");
  }

  // Order independence: permute frames, bit-identical to ascending reference.
  {
    std::vector<double> reference(32);
    for (std::uint64_t f = 0; f < reference.size(); ++f) {
      reference[static_cast<std::size_t>(f)] =
          sample_unit_noise(kSeed, node_a, prop, f);
    }
    const std::vector<std::uint64_t> order = {7, 0, 31, 2, 15, 1, 20, 9};
    for (std::uint64_t f : order) {
      const double u = sample_unit_noise(kSeed, node_a, prop, f);
      expect(std::memcmp(&u, &reference[static_cast<std::size_t>(f)],
                         sizeof(double)) == 0,
             "frame sample independent of call order");
    }
  }

  // Unrelated node must not change the original node's channel.
  {
    const double before = sample_unit_noise(kSeed, node_a, prop, 11);
    // Evaluate many samples for an unrelated node / property (simulates adding
    // a deco node to the scene graph and sampling its noise).
    double sink = 0.0;
    for (std::uint64_t f = 0; f < 128; ++f) {
      sink += sample_unit_noise(kSeed, node_b, "translate_x", f);
      sink += sample_unit_noise(kSeed, node_b, prop, f);
    }
    const double after = sample_unit_noise(kSeed, node_a, prop, 11);
    expect(std::memcmp(&before, &after, sizeof(double)) == 0,
           "unrelated node does not affect other channel");
    expect(std::isfinite(sink), "sink used");
  }

  // Distinct channels differ for the chosen fixture IDs.
  expect(hash_animation_channel(kSeed, node_a, prop, 0) !=
             hash_animation_channel(kSeed, node_b, prop, 0),
         "different nodes diverge");
  expect(hash_animation_channel(kSeed, node_a, prop, 0) !=
             hash_animation_channel(kSeed, node_a, "translate_x", 0),
         "different properties diverge");
  expect(hash_animation_channel(kSeed, node_a, prop, 0) !=
             hash_animation_channel(kSeed, node_a, prop, 1),
         "different frames diverge");
  expect(hash_animation_channel(kSeed, node_a, prop, 0) !=
             hash_animation_channel(kSeed ^ 1ULL, node_a, prop, 0),
         "different seeds diverge");

  // hash_to_unit endpoints of the bit pattern stay in range.
  expect_near(hash_to_unit(0), 0.0, 0.0, "zero hash -> 0");
  expect(hash_to_unit(~0ULL) < 1.0, "max hash < 1");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
