#pragma once

#include <instantee/diagnostics/error.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>

namespace instantee::spike::apple {

namespace detail {
struct PixelBufferAccess;
}

// Fixed 32BGRA buffers matching the V1 export geometry. Core Video types stay
// inside the adapter implementation.
struct PixelBufferFormat {
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return width > 0U && height > 0U;
  }

  [[nodiscard]] static constexpr PixelBufferFormat media_1080x1920() noexcept {
    return PixelBufferFormat{1080U, 1920U};
  }
};

// Move-only application ownership of one pool-backed pixel buffer.
// After seal(), mutation APIs fail; the encoder may still hold its own retain.
class PixelBuffer {
public:
  PixelBuffer(PixelBuffer &&) noexcept;
  PixelBuffer &operator=(PixelBuffer &&) noexcept;
  ~PixelBuffer();

  PixelBuffer(const PixelBuffer &) = delete;
  PixelBuffer &operator=(const PixelBuffer &) = delete;

  [[nodiscard]] PixelBufferFormat format() const noexcept;
  [[nodiscard]] bool sealed() const noexcept;

  // Marks the buffer handed to an encoder/sink. Further mutation fails.
  void seal() noexcept;

  // Writes tightly packed BGRA8 rows. Fails when sealed or size mismatches.
  [[nodiscard]] std::expected<void, Error>
  fill_bgra8(std::span<const std::uint8_t> tightly_packed);

  // Creates an additional owning retain of the same buffer.
  [[nodiscard]] std::expected<PixelBuffer, Error> retain_copy() const;

private:
  friend class PixelBufferPool;
  friend struct detail::PixelBufferAccess;
  struct Impl;
  explicit PixelBuffer(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

namespace detail {

// Opaque access used by the Apple writer adapter implementation only.
struct PixelBufferAccess {
  [[nodiscard]] static void *raw_buffer(PixelBuffer &buffer) noexcept;
  [[nodiscard]] static bool empty(const PixelBuffer &buffer) noexcept;
};

} // namespace detail

// Bounded CVPixelBufferPool with allocation threshold E. Exhausted acquires
// wait with stop/deadline awareness and never fall back to unbounded
// CVPixelBufferCreate allocations.
class PixelBufferPool {
public:
  PixelBufferPool(PixelBufferPool &&) noexcept;
  PixelBufferPool &operator=(PixelBufferPool &&) noexcept;
  ~PixelBufferPool();

  PixelBufferPool(const PixelBufferPool &) = delete;
  PixelBufferPool &operator=(const PixelBufferPool &) = delete;

  [[nodiscard]] static std::expected<PixelBufferPool, Error>
  create(PixelBufferFormat format, std::uint32_t capacity);

  [[nodiscard]] PixelBufferFormat format() const noexcept;
  [[nodiscard]] std::uint32_t capacity() const noexcept;
  [[nodiscard]] bool closed() const noexcept;

  // Wakes blocked acquire waiters with a closed result.
  void close() noexcept;

  [[nodiscard]] std::expected<PixelBuffer, Error>
  acquire(std::stop_token stop = {},
          std::optional<std::chrono::steady_clock::time_point> deadline =
              std::nullopt);

private:
  friend struct PixelBuffer::Impl;
  struct Impl;
  explicit PixelBufferPool(std::shared_ptr<Impl> impl) noexcept;

  std::shared_ptr<Impl> impl_;
};

} // namespace instantee::spike::apple
