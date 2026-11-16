#include <instantee/core_model/time.hpp>

#include <limits>
#include <numeric>

namespace instantee::core {
namespace {

const diagnostics::Error kInvalidRate{
    diagnostics::ExitCode::invalid_input, "invalid_frame_rate", "time",
    "Frame rate numerator and denominator must be non-zero"};

const diagnostics::Error kPtsOverflow{
    diagnostics::ExitCode::invalid_input, "pts_overflow", "time",
    "Presentation timestamp multiplication overflowed"};

const diagnostics::Error kFrameIndexOverflow{
    diagnostics::ExitCode::invalid_input, "frame_index_overflow", "time",
    "Frame index addition overflowed"};

[[nodiscard]] constexpr bool checked_mul(std::uint64_t lhs, std::uint64_t rhs,
                                         std::uint64_t &out) noexcept {
  if (lhs == 0U || rhs == 0U) {
    out = 0U;
    return true;
  }
  if (lhs > std::numeric_limits<std::uint64_t>::max() / rhs) {
    return false;
  }
  out = lhs * rhs;
  return true;
}

[[nodiscard]] constexpr unsigned __int128 widen(std::uint64_t value) noexcept {
  return static_cast<unsigned __int128>(value);
}

} // namespace

std::expected<FrameIndex, diagnostics::Error>
FrameIndex::checked_add(FrameIndex base, std::uint64_t delta) noexcept {
  if (delta > std::numeric_limits<std::uint64_t>::max() - base.value()) {
    return std::unexpected(kFrameIndexOverflow);
  }
  return FrameIndex{base.value() + delta};
}

std::expected<FrameRate, diagnostics::Error>
FrameRate::make(std::uint32_t numerator, std::uint32_t denominator) noexcept {
  if (numerator == 0U || denominator == 0U) {
    return std::unexpected(kInvalidRate);
  }
  return FrameRate{numerator, denominator};
}

std::expected<RationalTime, diagnostics::Error>
RationalTime::make(std::uint64_t numerator,
                   std::uint64_t denominator) noexcept {
  if (denominator == 0U) {
    return std::unexpected(diagnostics::Error{
        diagnostics::ExitCode::invalid_input, "invalid_rational_time", "time",
        "Rational time denominator must be non-zero"});
  }
  return RationalTime{numerator, denominator};
}

RationalTime RationalTime::reduced() const noexcept {
  const auto divisor = std::gcd(numerator_, denominator_);
  return RationalTime{numerator_ / divisor, denominator_ / divisor};
}

bool operator==(RationalTime lhs, RationalTime rhs) noexcept {
  return widen(lhs.numerator_) * rhs.denominator_ ==
         widen(rhs.numerator_) * lhs.denominator_;
}

std::strong_ordering operator<=>(RationalTime lhs, RationalTime rhs) noexcept {
  const auto left = widen(lhs.numerator_) * rhs.denominator_;
  const auto right = widen(rhs.numerator_) * lhs.denominator_;
  if (left < right) {
    return std::strong_ordering::less;
  }
  if (left > right) {
    return std::strong_ordering::greater;
  }
  return std::strong_ordering::equal;
}

std::expected<FrameClock, diagnostics::Error>
FrameClock::create(FrameRate rate) noexcept {
  if (!rate.valid()) {
    return std::unexpected(kInvalidRate);
  }
  return FrameClock{rate};
}

FrameClock FrameClock::media_30_1() noexcept {
  return FrameClock{FrameRate::media_30_1()};
}

std::expected<RationalTime, diagnostics::Error>
FrameClock::pts(FrameIndex index) const noexcept {
  std::uint64_t scaled = 0;
  if (!checked_mul(index.value(),
                   static_cast<std::uint64_t>(rate_.denominator()), scaled)) {
    return std::unexpected(kPtsOverflow);
  }
  return RationalTime::make(scaled, rate_.numerator());
}

std::expected<RationalTime, diagnostics::Error>
FrameClock::duration(FrameIndex frame_count) const noexcept {
  // Duration of N frames equals the PTS of frame index N (exclusive end).
  return pts(frame_count);
}

} // namespace instantee::core
