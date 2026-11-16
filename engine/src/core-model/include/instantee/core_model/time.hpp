#pragma once

#include <instantee/diagnostics/error.hpp>

#include <compare>
#include <cstdint>
#include <expected>
#include <limits>

namespace instantee::core {

// Strong zero-based frame ordinal. Arithmetic that can overflow goes through
// checked helpers; silent uint64 truncation is rejected.
class FrameIndex {
public:
  constexpr FrameIndex() noexcept = default;
  constexpr explicit FrameIndex(std::uint64_t value) noexcept : value_(value) {}

  [[nodiscard]] static constexpr FrameIndex max() noexcept {
    return FrameIndex{std::numeric_limits<std::uint64_t>::max()};
  }

  [[nodiscard]] constexpr std::uint64_t value() const noexcept {
    return value_;
  }

  [[nodiscard]] friend constexpr bool operator==(FrameIndex,
                                                 FrameIndex) noexcept = default;
  [[nodiscard]] friend constexpr std::strong_ordering
  operator<=>(FrameIndex, FrameIndex) noexcept = default;

  constexpr FrameIndex &operator++() noexcept {
    ++value_;
    return *this;
  }

  constexpr FrameIndex operator++(int) noexcept {
    const FrameIndex prior = *this;
    ++value_;
    return prior;
  }

  [[nodiscard]] static std::expected<FrameIndex, diagnostics::Error>
  checked_add(FrameIndex base, std::uint64_t delta) noexcept;

private:
  std::uint64_t value_ = 0;
};

// Frames per second as an exact rational. Invalid zero rates cannot be built
// through make(); media_30_1() is the only public constant constructor.
class FrameRate {
public:
  [[nodiscard]] static constexpr FrameRate media_30_1() noexcept {
    return FrameRate{30U, 1U};
  }

  [[nodiscard]] static std::expected<FrameRate, diagnostics::Error>
  make(std::uint32_t numerator, std::uint32_t denominator) noexcept;

  [[nodiscard]] constexpr std::uint32_t numerator() const noexcept {
    return numerator_;
  }
  [[nodiscard]] constexpr std::uint32_t denominator() const noexcept {
    return denominator_;
  }

  [[nodiscard]] constexpr bool valid() const noexcept {
    return numerator_ != 0U && denominator_ != 0U;
  }

  [[nodiscard]] friend constexpr bool operator==(FrameRate,
                                                 FrameRate) noexcept = default;

private:
  constexpr FrameRate(std::uint32_t numerator,
                      std::uint32_t denominator) noexcept
      : numerator_(numerator), denominator_(denominator) {}

  std::uint32_t numerator_ = 0;
  std::uint32_t denominator_ = 0;
};

// Non-negative rational presentation time in seconds: numerator/denominator.
// Values are stored unreduced; equality and ordering use exact cross products.
class RationalTime {
public:
  RationalTime() = default;

  [[nodiscard]] static RationalTime zero() noexcept {
    return RationalTime{0, 1};
  }

  [[nodiscard]] static std::expected<RationalTime, diagnostics::Error>
  make(std::uint64_t numerator, std::uint64_t denominator) noexcept;

  [[nodiscard]] constexpr std::uint64_t numerator() const noexcept {
    return numerator_;
  }
  [[nodiscard]] constexpr std::uint64_t denominator() const noexcept {
    return denominator_;
  }

  [[nodiscard]] RationalTime reduced() const noexcept;

private:
  friend bool operator==(RationalTime lhs, RationalTime rhs) noexcept;
  friend std::strong_ordering operator<=>(RationalTime lhs,
                                          RationalTime rhs) noexcept;

  constexpr RationalTime(std::uint64_t numerator,
                         std::uint64_t denominator) noexcept
      : numerator_(numerator), denominator_(denominator) {}

  std::uint64_t numerator_ = 0;
  std::uint64_t denominator_ = 1;
};

[[nodiscard]] bool operator==(RationalTime lhs, RationalTime rhs) noexcept;
[[nodiscard]] std::strong_ordering operator<=>(RationalTime lhs,
                                               RationalTime rhs) noexcept;

// Converts indexed frames to monotonic PTS without floating-point accumulation.
// Contract: pts(frame) = frame * rate.denominator / rate.numerator (exact).
class FrameClock {
public:
  [[nodiscard]] static std::expected<FrameClock, diagnostics::Error>
  create(FrameRate rate) noexcept;

  [[nodiscard]] static FrameClock media_30_1() noexcept;

  [[nodiscard]] constexpr FrameRate rate() const noexcept { return rate_; }

  // Start time of frame `index`.
  [[nodiscard]] std::expected<RationalTime, diagnostics::Error>
  pts(FrameIndex index) const noexcept;

  // Exclusive end time after `frame_count` frames (media duration).
  [[nodiscard]] std::expected<RationalTime, diagnostics::Error>
  duration(FrameIndex frame_count) const noexcept;

private:
  explicit constexpr FrameClock(FrameRate rate) noexcept : rate_(rate) {}

  FrameRate rate_{FrameRate::media_30_1()};
};

} // namespace instantee::core
