#include <instantee/core_model/integer_encoding.hpp>

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  using instantee::core::ContractDefaultsV1;
  using instantee::core::kJsSafeIntegerMax;
  using instantee::core::Uint64Encoding;
  using instantee::core::Uint64JsonForm;

  expect(kJsSafeIntegerMax == 9007199254740991ULL, "JS safe max constant");

  const auto zero = Uint64Encoding::parse_decimal("0");
  expect(zero.has_value() && *zero == 0ULL, "parse 0");

  const auto safe = Uint64Encoding::from_json_number(kJsSafeIntegerMax);
  expect(safe.has_value() && *safe == kJsSafeIntegerMax, "number at safe max");

  const auto above = Uint64Encoding::from_json_number(kJsSafeIntegerMax + 1ULL);
  expect(!above.has_value(), "reject number above safe max");

  const auto u64_max = Uint64Encoding::parse_decimal("18446744073709551615");
  expect(u64_max.has_value() &&
             *u64_max == std::numeric_limits<std::uint64_t>::max(),
         "parse uint64 max");
  expect(Uint64Encoding::format_decimal(*u64_max) == "18446744073709551615",
         "format uint64 max");

  expect(!Uint64Encoding::parse_decimal("18446744073709551616").has_value(),
         "reject uint64 overflow");
  expect(!Uint64Encoding::parse_decimal("042").has_value(),
         "reject leading zero");
  expect(!Uint64Encoding::parse_decimal("").has_value(), "reject empty");
  expect(!Uint64Encoding::parse_decimal("-1").has_value(), "reject sign");

  expect(Uint64Encoding::preferred_form(42) == Uint64JsonForm::number,
         "prefer number for small values");
  expect(Uint64Encoding::preferred_form(
             std::numeric_limits<std::uint64_t>::max()) ==
             Uint64JsonForm::decimal_string,
         "prefer string for full uint64");

  expect(ContractDefaultsV1::node_visible, "default visible");
  expect(ContractDefaultsV1::transform_opacity == 1.0, "default opacity");
  expect(ContractDefaultsV1::camera_near == 0.1, "default near");
  expect(ContractDefaultsV1::camera_far == 1000.0, "default far");
  expect(ContractDefaultsV1::camera_projection == "orthographic",
         "default projection");
  expect(ContractDefaultsV1::canvas_color == "rec709-sdr", "default canvas");
  expect(ContractDefaultsV1::transition_overlap_frames == 0U,
         "default overlap");
  expect(ContractDefaultsV1::transition_blend == "cut", "default blend");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
