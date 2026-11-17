#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/resource_registry.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace instantee::render_core {

// What happens before a recycled target is handed to the next borrower.
// Never changes width/height: budget failures are errors, not silent
// downscales.
enum class RenderTargetClearPolicy : std::uint8_t {
  clear_to_transparent = 1,
  clear_to_color = 2,
};

struct RenderTargetPoolConfig {
  std::uint32_t registry_capacity = GpuResourceRegistry::kDefaultCapacity;
  // Soft working-set budget for pooled RGBA8 targets (bytes). New allocations
  // that would exceed this fail with resource_budget_exceeded.
  std::uint64_t max_pooled_bytes = 64ULL * 1024ULL * 1024ULL;
  // Reject a single allocation larger than this even when the pool is empty.
  std::uint64_t max_allocation_bytes = 64ULL * 1024ULL * 1024ULL;
  RenderTargetClearPolicy clear_policy =
      RenderTargetClearPolicy::clear_to_transparent;
  Rgba8 clear_color{.r = 0, .g = 0, .b = 0, .a = 0};
};

// Counters for recycling proofs. Units are counts / bytes, not OS RSS; process
// footprint evidence stays outside this type until a measurement task.
struct RenderTargetPoolStats {
  std::uint64_t allocations_created = 0;
  std::uint64_t acquires_total = 0;
  std::uint64_t acquires_reused = 0;
  std::uint64_t releases_total = 0;
  std::uint64_t clears_performed = 0;
  std::uint64_t bytes_currently_pooled = 0;
  std::uint64_t bytes_high_water = 0;
  std::uint32_t free_list_size = 0;
  std::uint32_t in_use_count = 0;
};

class RenderTargetPool;

// Move-only lease over a pooled color target. Destruction / release() returns
// the target to the pool after applying the clear policy.
class RenderTargetLease {
public:
  RenderTargetLease() noexcept = default;
  RenderTargetLease(RenderTargetLease &&other) noexcept;
  RenderTargetLease &operator=(RenderTargetLease &&other) noexcept;
  ~RenderTargetLease();

  RenderTargetLease(const RenderTargetLease &) = delete;
  RenderTargetLease &operator=(const RenderTargetLease &) = delete;

  [[nodiscard]] bool empty() const noexcept { return handle_.value == 0; }
  [[nodiscard]] explicit operator bool() const noexcept { return !empty(); }

  [[nodiscard]] GpuResourceHandle handle() const noexcept { return handle_; }
  [[nodiscard]] GpuResourceDesc desc() const noexcept { return desc_; }

  // Borrowed RGBA8 view valid until release. Origin top-left, tightly packed.
  [[nodiscard]] std::expected<std::span<std::uint8_t>, diagnostics::Error>
  pixels();

  // Returns the target to the pool (cleared). Safe to call repeatedly.
  void release() noexcept;

private:
  friend class RenderTargetPool;
  RenderTargetLease(RenderTargetPool *pool, GpuResourceHandle handle,
                    GpuResourceDesc desc) noexcept;

  RenderTargetPool *pool_ = nullptr;
  GpuResourceHandle handle_{};
  GpuResourceDesc desc_{};
};

// Bounded pool of compatible RGBA8 render targets backed by
// GpuResourceRegistry. Compatible == identical width/height. Lifetime: in-use
// leases keep the registry handle alive; freed targets stay resident on an
// internal free list until the pool is destroyed.
class RenderTargetPool {
public:
  RenderTargetPool(RenderTargetPool &&) noexcept;
  RenderTargetPool &operator=(RenderTargetPool &&) noexcept;
  ~RenderTargetPool();

  RenderTargetPool(const RenderTargetPool &) = delete;
  RenderTargetPool &operator=(const RenderTargetPool &) = delete;

  [[nodiscard]] static std::expected<RenderTargetPool, diagnostics::Error>
  create(RenderTargetPoolConfig config = {});

  [[nodiscard]] const RenderTargetPoolConfig &config() const noexcept {
    return config_;
  }
  [[nodiscard]] RenderTargetPoolStats stats() const noexcept { return stats_; }
  [[nodiscard]] GpuResourceRegistry &registry() noexcept { return *registry_; }
  [[nodiscard]] const GpuResourceRegistry &registry() const noexcept {
    return *registry_;
  }

  // Acquires a cleared target. Prefers a free-list match; otherwise allocates
  // when budget and registry capacity allow. Never resizes to fit budget.
  [[nodiscard]] std::expected<RenderTargetLease, diagnostics::Error>
  acquire(GpuResourceDesc desc);

private:
  friend class RenderTargetLease;

  struct PooledTarget final : GpuResourcePayload {
    std::uint32_t width_px = 0;
    std::uint32_t height_px = 0;
    std::vector<std::uint8_t> rgba{};
  };

  explicit RenderTargetPool(RenderTargetPoolConfig config,
                            std::unique_ptr<GpuResourceRegistry> registry);

  void recycle(GpuResourceHandle handle, GpuResourceDesc desc) noexcept;
  [[nodiscard]] static std::expected<std::uint64_t, diagnostics::Error>
  rgba_bytes(GpuResourceDesc desc);
  void apply_clear(PooledTarget &target) noexcept;
  void note_bytes_delta(std::int64_t delta) noexcept;

  RenderTargetPoolConfig config_{};
  std::unique_ptr<GpuResourceRegistry> registry_{};
  std::vector<GpuResourceHandle> free_list_{};
  RenderTargetPoolStats stats_{};
};

} // namespace instantee::render_core
