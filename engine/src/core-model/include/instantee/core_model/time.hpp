#pragma once

#include <instantee/diagnostics/error.hpp>

#include <compare>
#include <cstdint>
#include <expected>

namespace instantee::core {

using FrameIndex = std::uint64_t;

// Frames per second as an exact rational. V1 media uses 30/1.
struct FrameRate {
  std::uint32_t numerator = 0;
  std::uint32_t denominator = 0;

  [[nodiscard]] static constexpr FrameRate media_30_1() noexcept {
    return FrameRate{30U, 1U};
  }

  [[nodiscard]] constexpr bool valid() const noexcept {
    return numerator != 0U && denominator != 0U;
  }

  [[nodiscard]] friend constexpr bool operator==(FrameRate,
                                                 FrameRate) noexcept = default;
};

// Non-negative rational presentation time in seconds: numerator/denominator.
// Values are stored unreduced; equality and ordering use exact cross products.
struct RationalTime {
  std::uint64_t numerator = 0;
  std::uint64_t denominator = 1;

  [[nodiscard]] static std::expected<RationalTime, spike::Error>
  make(std::uint64_t numerator, std::uint64_t denominator) noexcept;

  [[nodiscard]] RationalTime reduced() const noexcept;
};

[[nodiscard]] bool operator==(RationalTime lhs, RationalTime rhs) noexcept;
[[nodiscard]] std::strong_ordering operator<=>(RationalTime lhs,
                                               RationalTime rhs) noexcept;

// Converts indexed frames to monotonic PTS without floating-point accumulation.
// Contract: pts(frame) = frame * rate.denominator / rate.numerator (exact).
class FrameClock {
public:
  [[nodiscard]] static std::expected<FrameClock, spike::Error>
  create(FrameRate rate) noexcept;

  [[nodiscard]] static FrameClock media_30_1() noexcept;

  [[nodiscard]] constexpr FrameRate rate() const noexcept { return rate_; }

  // Start time of frame `index`.
  [[nodiscard]] std::expected<RationalTime, spike::Error>
  pts(FrameIndex index) const noexcept;

  // Exclusive end time after `frame_count` frames (media duration).
  [[nodiscard]] std::expected<RationalTime, spike::Error>
  duration(FrameIndex frame_count) const noexcept;

private:
  explicit constexpr FrameClock(FrameRate rate) noexcept : rate_(rate) {}

  FrameRate rate_{};
};

} // namespace instantee::core
