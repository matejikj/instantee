#pragma once

#include <instantee/core_model/time.hpp>
#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>

namespace instantee::core {

struct FrameConversion {
  FrameIndex frame{};
  // True when the start PTS of `frame` is not exactly the input seconds.
  bool sub_frame_loss = false;
};

struct FrameInterval {
  FrameIndex start{};
  FrameIndex end_exclusive{};
  bool sub_frame_loss = false;
};

// Nearest-frame conversion with half-away-from-zero on the exact rational
// seconds * numerator / denominator. Silent floating truncation is rejected:
// callers must pass an exact RationalTime (or use from_seconds_micros).
[[nodiscard]] std::expected<FrameConversion, diagnostics::Error>
seconds_to_frame(RationalTime seconds, FrameRate rate) noexcept;

// Convenience for whole-microsecond durations (common authoring input).
[[nodiscard]] std::expected<FrameConversion, diagnostics::Error>
seconds_micros_to_frame(std::uint64_t microseconds, FrameRate rate) noexcept;

// End-exclusive interval [start, end) in frames.
[[nodiscard]] std::expected<FrameInterval, diagnostics::Error>
seconds_interval_to_frames(RationalTime start, RationalTime end,
                           FrameRate rate) noexcept;

} // namespace instantee::core
