#include <instantee/core_model/time_convert.hpp>

#include <cstdlib>
#include <iostream>

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
  using instantee::core::FrameRate;
  using instantee::core::RationalTime;
  using instantee::core::seconds_interval_to_frames;
  using instantee::core::seconds_micros_to_frame;
  using instantee::core::seconds_to_frame;

  const auto rate = FrameRate::media_30_1();

  // 1.0s at 30/1 → frame 30
  auto one = RationalTime::make(1, 1);
  expect(one.has_value(), "make 1s");
  if (one) {
    const auto frame = seconds_to_frame(*one, rate);
    expect(frame.has_value() && frame->frame.value() == 30ULL, "1s → frame 30");
    expect(frame && !frame->sub_frame_loss, "exact second has no loss");
  }

  // 0.5s → frame 15
  auto half = RationalTime::make(1, 2);
  expect(half.has_value(), "make 0.5s");
  if (half) {
    const auto frame = seconds_to_frame(*half, rate);
    expect(frame.has_value() && frame->frame.value() == 15ULL, "0.5s → 15");
  }

  // Half-up: 1/60 s at 30fps → 0.5 frames → rounds to 1
  auto sixtieth = RationalTime::make(1, 60);
  expect(sixtieth.has_value(), "make 1/60");
  if (sixtieth) {
    const auto frame = seconds_to_frame(*sixtieth, rate);
    expect(frame.has_value() && frame->frame.value() == 1ULL,
           "1/60s half-away → 1");
    expect(frame && frame->sub_frame_loss, "sub-frame loss flagged");
  }

  const auto micros = seconds_micros_to_frame(1'000'000ULL, rate);
  expect(micros.has_value() && micros->frame.value() == 30ULL, "1e6 µs → 30");

  if (one && half) {
    const auto interval = seconds_interval_to_frames(*half, *one, rate);
    expect(interval.has_value(), "interval");
    if (interval) {
      expect(interval->start.value() == 15ULL, "interval start");
      expect(interval->end_exclusive.value() == 30ULL,
             "interval end exclusive");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
