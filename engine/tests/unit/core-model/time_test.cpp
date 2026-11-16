#include <instantee/core_model/time.hpp>

#include <cstdint>
#include <limits>

namespace {

using instantee::core::FrameClock;
using instantee::core::FrameIndex;
using instantee::core::FrameRate;
using instantee::core::RationalTime;
using instantee::spike::ExitCode;

[[nodiscard]] bool expect_eq(RationalTime actual, RationalTime expected) {
  return actual == expected;
}

[[nodiscard]] int check_media_timeline() {
  const auto clock = FrameClock::media_30_1();
  if (clock.rate() != FrameRate::media_30_1()) {
    return 1;
  }

  constexpr FrameIndex frame_count = 1800;
  RationalTime previous{0, 1};
  for (FrameIndex index = 0; index < frame_count; ++index) {
    const auto pts = clock.pts(index);
    if (!pts) {
      return 2;
    }
    const RationalTime expected{index, 30};
    if (!expect_eq(*pts, expected)) {
      return 3;
    }
    if (index > 0 && !(*pts > previous)) {
      return 4;
    }
    previous = *pts;
  }

  const auto duration = clock.duration(frame_count);
  if (!duration || !expect_eq(*duration, RationalTime{60, 1}) ||
      !expect_eq(*duration, RationalTime{1800, 30}) ||
      duration->reduced() != RationalTime{60, 1}) {
    return 5;
  }

  // Exclusive end of the last frame equals the media duration.
  const auto last_start = clock.pts(frame_count - 1);
  const auto last_end = clock.pts(frame_count);
  if (!last_start || !last_end || !(*last_end > *last_start) ||
      !expect_eq(*last_end, *duration)) {
    return 6;
  }
  return 0;
}

[[nodiscard]] int check_fractional_rate() {
  const auto clock = FrameClock::create(FrameRate{30000U, 1001U});
  if (!clock) {
    return 7;
  }
  const auto pts = clock->pts(30000);
  const auto duration = clock->duration(30000);
  if (!pts || !duration || !expect_eq(*pts, RationalTime{1001, 1}) ||
      !expect_eq(*duration, RationalTime{1001, 1})) {
    return 8;
  }
  return 0;
}

[[nodiscard]] int check_invalid_inputs() {
  for (auto rate : {FrameRate{0U, 1U}, FrameRate{30U, 0U}, FrameRate{0U, 0U}}) {
    if (FrameClock::create(rate)) {
      return 9;
    }
  }
  if (RationalTime::make(1, 0)) {
    return 10;
  }
  return 0;
}

[[nodiscard]] int check_overflow() {
  const auto clock = FrameClock::create(FrameRate{1U, 2U});
  if (!clock) {
    return 11;
  }
  const auto overflow =
      clock->pts(std::numeric_limits<FrameIndex>::max() / 2U + 1U);
  if (overflow || overflow.error().exit_code != ExitCode::invalid_input ||
      overflow.error().code != "pts_overflow") {
    return 12;
  }

  const auto edge = clock->pts(std::numeric_limits<FrameIndex>::max() / 2U);
  if (!edge ||
      !expect_eq(
          *edge,
          RationalTime{std::numeric_limits<FrameIndex>::max() / 2U * 2U, 1})) {
    return 13;
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
  return check_overflow();
}
