#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace instantee::render_core {

// Fixed-format CPU staging / readback pool. Capacity is a hard allocation
// threshold (E-style): exhausted acquires return would_block / budget errors
// and never grow with a fallback allocation.
struct StagingPoolConfig {
  std::uint32_t capacity = 2;
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
};

struct StagingPoolStats {
  std::uint32_t capacity = 0;
  std::uint32_t in_use = 0;
  std::uint32_t free_count = 0;
  std::uint64_t acquires = 0;
  std::uint64_t acquire_rejections = 0;
  std::uint64_t recycles = 0;
};

class StagingBufferPool;

// Move-only staging lease. GPU and sink holds keep the backing store alive and
// immutable until the matching complete_* / cancel drain; cancel never clears
// bytes while a hold remains.
class StagingLease {
public:
  enum class State : std::uint8_t {
    writable = 1,
    gpu_busy = 2,
    sink_busy = 3,
    gpu_and_sink_busy = 4,
    cancelled = 5,
    consumed = 6,
  };

  StagingLease() noexcept = default;
  StagingLease(StagingLease &&other) noexcept;
  StagingLease &operator=(StagingLease &&other) noexcept;
  ~StagingLease();

  StagingLease(const StagingLease &) = delete;
  StagingLease &operator=(const StagingLease &) = delete;

  [[nodiscard]] bool empty() const noexcept {
    return pool_ == nullptr ||
           index_ == std::numeric_limits<std::uint32_t>::max();
  }
  [[nodiscard]] explicit operator bool() const noexcept { return !empty(); }
  [[nodiscard]] State state() const noexcept;

  [[nodiscard]] std::uint32_t width_px() const noexcept;
  [[nodiscard]] std::uint32_t height_px() const noexcept;
  [[nodiscard]] std::size_t byte_size() const noexcept;

  // Mutable view: only while writable (no GPU/sink hold, not cancelled).
  [[nodiscard]] std::expected<std::span<std::uint8_t>, diagnostics::Error>
  pixels();

  // Read-only view valid until the lease recycles. Allowed under GPU/sink
  // holds so delayed callbacks can still observe the frozen bytes.
  [[nodiscard]] std::expected<std::span<const std::uint8_t>, diagnostics::Error>
  frozen_bytes() const;

  [[nodiscard]] std::expected<void, diagnostics::Error>
  fill_rgba8(std::span<const std::uint8_t> tightly_packed);

  [[nodiscard]] std::expected<void, diagnostics::Error> mark_gpu_busy();
  [[nodiscard]] std::expected<void, diagnostics::Error> complete_gpu();
  [[nodiscard]] std::expected<void, diagnostics::Error> mark_sink_busy();
  [[nodiscard]] std::expected<void, diagnostics::Error> complete_sink();

  // Refuse further mutation/delivery. Does not clear or recycle while GPU/sink
  // holds remain.
  void cancel() noexcept;

  // Returns the slot when idle. Abandons holds if still outstanding.
  void release() noexcept;

private:
  friend class StagingBufferPool;

  explicit StagingLease(StagingBufferPool *pool, std::uint32_t index) noexcept;
  void recycle_if_idle() noexcept;

  StagingBufferPool *pool_ = nullptr;
  std::uint32_t index_ = std::numeric_limits<std::uint32_t>::max();
};

class StagingBufferPool {
public:
  StagingBufferPool(StagingBufferPool &&) noexcept;
  StagingBufferPool &operator=(StagingBufferPool &&) noexcept;
  ~StagingBufferPool();

  StagingBufferPool(const StagingBufferPool &) = delete;
  StagingBufferPool &operator=(const StagingBufferPool &) = delete;

  [[nodiscard]] static std::expected<StagingBufferPool, diagnostics::Error>
  create(StagingPoolConfig config);

  [[nodiscard]] const StagingPoolConfig &config() const noexcept {
    return config_;
  }
  [[nodiscard]] StagingPoolStats stats() const noexcept;

  // Non-blocking acquire. At capacity returns would_block and never allocates
  // an additional staging buffer past the configured threshold.
  [[nodiscard]] std::expected<StagingLease, diagnostics::Error> try_acquire();

private:
  friend class StagingLease;

  struct Slot {
    std::vector<std::uint8_t> rgba{};
    bool checked_out = false;
    bool gpu_hold = false;
    bool sink_hold = false;
    bool cancelled = false;
  };

  explicit StagingBufferPool(StagingPoolConfig config, std::vector<Slot> slots);

  [[nodiscard]] Slot *slot_at(std::uint32_t index) noexcept;
  [[nodiscard]] const Slot *slot_at(std::uint32_t index) const noexcept;
  void note_recycle() noexcept;

  StagingPoolConfig config_{};
  std::vector<Slot> slots_{};
  StagingPoolStats stats_{};
};

// Copies tightly packed RGBA8 from staging into a caller-owned sink buffer
// (CPU stand-in for staging → encoder pixel buffer). Does not release either
// side.
[[nodiscard]] std::expected<void, diagnostics::Error>
copy_staging_to_sink(std::span<const std::uint8_t> staging_rgba,
                     std::span<std::uint8_t> sink_rgba);

} // namespace instantee::render_core
