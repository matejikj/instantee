#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>
#include <instantee/render_metal/device.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace instantee::spike::transfer {
using diagnostics::Error;
using diagnostics::ExitCode;

enum class FrameTransferKind : std::uint8_t { cpu_staging, iosurface_shared };

// Move-only GPU submission that renders the flattened test pattern into an
// IOSurface-backed, Metal-compatible pixel buffer. The lease retains the
// command buffer, CVMetalTexture and pixel buffer until GPU completion,
// cancel retirement, or destruction. Destroying the lease never recycles the
// pixel buffer while GPU work remains in flight.
class SharedSurfaceLease {
public:
  enum class State : std::uint8_t {
    in_flight,
    ready,
    failed,
    cancelled,
    consumed
  };

  SharedSurfaceLease(SharedSurfaceLease &&) noexcept;
  SharedSurfaceLease &operator=(SharedSurfaceLease &&) noexcept;
  ~SharedSurfaceLease();

  SharedSurfaceLease(const SharedSurfaceLease &) = delete;
  SharedSurfaceLease &operator=(const SharedSurfaceLease &) = delete;

  [[nodiscard]] State state() const noexcept;
  [[nodiscard]] std::uint32_t frame_index() const noexcept;

  // Refuse later buffer delivery. Resources stay alive until GPU completion.
  void cancel() noexcept;

  // Waits for GPU completion when needed, then copies tightly packed BGRA8.
  // Fails after cancel or after a successful prior take.
  [[nodiscard]] std::expected<std::vector<std::uint8_t>, Error> take_bgra8();

  // Waits for GPU completion, seals the buffer, and transfers application
  // ownership for encoder handoff. The buffer must not be mutated afterward.
  [[nodiscard]] std::expected<apple::PixelBuffer, Error> take_pixel_buffer();

private:
  friend class SharedSurfaceRenderer;
  struct Impl;
  explicit SharedSurfaceLease(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

// Owns the BGRA shared-surface pipeline, CVMetalTextureCache and a bounded
// Metal/IOSurface pixel-buffer pool. Borrows the Metal device/queue for the
// renderer lifetime.
class SharedSurfaceRenderer {
public:
  SharedSurfaceRenderer(SharedSurfaceRenderer &&) noexcept;
  SharedSurfaceRenderer &operator=(SharedSurfaceRenderer &&) noexcept;
  ~SharedSurfaceRenderer();

  SharedSurfaceRenderer(const SharedSurfaceRenderer &) = delete;
  SharedSurfaceRenderer &operator=(const SharedSurfaceRenderer &) = delete;

  // device must outlive this renderer. capacity is the pool allocation
  // threshold E for IOSurface-backed buffers.
  [[nodiscard]] static std::expected<SharedSurfaceRenderer, Error>
  create(metal::Device &device, std::uint32_t capacity);

  [[nodiscard]] apple::PixelBufferCompatibility compatibility() const noexcept;
  [[nodiscard]] std::uint32_t capacity() const noexcept;

  // Encodes one flattened frame into a pooled shared surface. Supported
  // frames: 0 and 1. Exhausted pool acquires wait with stop/deadline support.
  [[nodiscard]] std::expected<SharedSurfaceLease, Error>
  submit(std::uint32_t frame_index, std::stop_token stop = {},
         std::optional<std::chrono::steady_clock::time_point> deadline =
             std::nullopt);

private:
  struct Impl;
  explicit SharedSurfaceRenderer(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

// Exact nearest-rank latency helper for the isolated transfer comparison.
[[nodiscard]] std::uint64_t
nearest_rank_latency_ns(std::span<const std::uint64_t> sorted_samples,
                        double quantile) noexcept;

} // namespace instantee::spike::transfer
