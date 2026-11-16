#pragma once

#include <compare>
#include <cstdint>

namespace instantee::core {

// Explicit pixel extent component. Not a screen coordinate space.
struct PixelCount {
  std::uint32_t value = 0;

  [[nodiscard]] friend constexpr bool operator==(PixelCount,
                                                 PixelCount) noexcept = default;
  [[nodiscard]] friend constexpr std::strong_ordering
  operator<=>(PixelCount, PixelCount) noexcept = default;
};

// Byte sizes for buffers and working-set reports.
struct ByteCount {
  std::uint64_t value = 0;

  [[nodiscard]] friend constexpr bool operator==(ByteCount,
                                                 ByteCount) noexcept = default;
  [[nodiscard]] friend constexpr std::strong_ordering
  operator<=>(ByteCount, ByteCount) noexcept = default;
};

} // namespace instantee::core
