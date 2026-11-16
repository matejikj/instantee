#include <instantee/core_model/time.hpp>
#include <instantee/core_model/units.hpp>

#include <cstdint>
#include <limits>

namespace {

using instantee::core::ByteCount;
using instantee::core::FrameClock;
using instantee::core::FrameIndex;
using instantee::core::FrameRate;
using instantee::core::PixelCount;
using instantee::core::RationalTime;
using instantee::diagnostics::ExitCode;

[[nodiscard]] RationalTime rt(std::uint64_t numerator,
                              std::uint64_t denominator) {
  return *RationalTime::make(numerator, denominator);
}

[[nodiscard]] bool expect_eq(RationalTime actual, RationalTime expected) {
  return actual == expected;
}

[[nodiscard]] int check_media_timeline() {
  const auto clock = FrameClock::media_30_1();
  if (clock.rate() != FrameRate::media_30_1()) {
    return 1;
  }

  constexpr FrameIndex frame_count{1800};
  RationalTime previous = rt(0, 1);
  for (FrameIndex index{0}; index < frame_count; ++index) {
    const auto pts = clock.pts(index);
    if (!pts) {
      return 2;
    }
    const RationalTime expected = rt(index.value(), 30);
    if (!expect_eq(*pts, expected)) {
      return 3;
    }
    if (index > FrameIndex{0} && !(*pts > previous)) {
      return 4;
    }
    previous = *pts;
  }

  const auto duration = clock.duration(frame_count);
  if (!duration || !expect_eq(*duration, rt(60, 1)) ||
      !expect_eq(*duration, rt(1800, 30)) || duration->reduced() != rt(60, 1)) {
    return 5;
  }

  const auto last_start = clock.pts(FrameIndex{frame_count.value() - 1});
  const auto last_end = clock.pts(frame_count);
  if (!last_start || !last_end || !(*last_end > *last_start) ||
      !expect_eq(*last_end, *duration)) {
    return 6;
  }
  return 0;
}

[[nodiscard]] int check_fractional_rate() {
  const auto rate = FrameRate::make(30000U, 1001U);
  if (!rate) {
    return 7;
  }
  const auto clock = FrameClock::create(*rate);
  if (!clock) {
    return 7;
  }
  const auto pts = clock->pts(FrameIndex{30000});
  const auto duration = clock->duration(FrameIndex{30000});
  if (!pts || !duration || !expect_eq(*pts, rt(1001, 1)) ||
      !expect_eq(*duration, rt(1001, 1))) {
    return 8;
  }
  return 0;
}

[[nodiscard]] int check_invalid_inputs() {
  for (auto [numerator, denominator] :
       {std::pair{0U, 1U}, std::pair{30U, 0U}, std::pair{0U, 0U}}) {
    if (FrameRate::make(numerator, denominator)) {
      return 9;
    }
  }
  if (RationalTime::make(1, 0)) {
    return 10;
  }
  return 0;
}

[[nodiscard]] int check_overflow() {
  const auto rate = FrameRate::make(1U, 2U);
  if (!rate) {
    return 11;
  }
  const auto clock = FrameClock::create(*rate);
  if (!clock) {
    return 11;
  }
  const auto overflow =
      clock->pts(FrameIndex{FrameIndex::max().value() / 2U + 1U});
  if (overflow || overflow.error().exit_code != ExitCode::invalid_input ||
      overflow.error().code != "pts_overflow") {
    return 12;
  }

  const auto edge = clock->pts(FrameIndex{FrameIndex::max().value() / 2U});
  if (!edge || !expect_eq(*edge, rt(FrameIndex::max().value() / 2U * 2U, 1))) {
    return 13;
  }

  if (FrameIndex::checked_add(FrameIndex::max(), 1)) {
    return 14;
  }
  return 0;
}

[[nodiscard]] int check_units() {
  if (PixelCount{1920} != PixelCount{1920} || PixelCount{1} >= PixelCount{2} ||
      ByteCount{8} != ByteCount{8}) {
    return 15;
  }
  return 0;
}

} // namespace

int main() {
  if (const int code = check_media_timeline(); code != 0) {
    return code;
  }
  if (const int code = check_fractional_rate(); code != 0) {
    return code;
  }
  if (const int code = check_invalid_inputs(); code != 0) {
    return code;
  }
  if (const int code = check_overflow(); code != 0) {
    return code;
  }
  return check_units();
}
