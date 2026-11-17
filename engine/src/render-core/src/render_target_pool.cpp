#include <instantee/render_core/render_target_pool.hpp>

#include <algorithm>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace instantee::render_core {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "render_target_pool", message};
}

[[nodiscard]] diagnostics::Error make_budget(std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::resource_budget_exceeded,
                            diagnostics::ErrorKind::resource_budget_exceeded,
                            "resource_budget_exceeded", "render_target_pool",
                            message};
}

} // namespace

RenderTargetLease::RenderTargetLease(RenderTargetPool *pool,
                                     GpuResourceHandle handle,
                                     GpuResourceDesc desc) noexcept
    : pool_(pool), handle_(handle), desc_(desc) {}

RenderTargetLease::RenderTargetLease(RenderTargetLease &&other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)),
      handle_(std::exchange(other.handle_, GpuResourceHandle{})),
      desc_(std::exchange(other.desc_, GpuResourceDesc{})) {}

RenderTargetLease &
RenderTargetLease::operator=(RenderTargetLease &&other) noexcept {
  if (this != &other) {
    release();
    pool_ = std::exchange(other.pool_, nullptr);
    handle_ = std::exchange(other.handle_, GpuResourceHandle{});
    desc_ = std::exchange(other.desc_, GpuResourceDesc{});
  }
  return *this;
}

RenderTargetLease::~RenderTargetLease() { release(); }

void RenderTargetLease::release() noexcept {
  if (pool_ == nullptr || !handle_.valid()) {
    pool_ = nullptr;
    handle_ = {};
    desc_ = {};
    return;
  }
  pool_->recycle(handle_, desc_);
  pool_ = nullptr;
  handle_ = {};
  desc_ = {};
}

std::expected<std::span<std::uint8_t>, diagnostics::Error>
RenderTargetLease::pixels() {
  if (pool_ == nullptr || !handle_.valid()) {
    return std::unexpected(
        make_invalid("empty_render_target_lease", "lease has no target"));
  }
  auto payload =
      pool_->registry_->try_get_as<RenderTargetPool::PooledTarget>(handle_);
  if (!payload) {
    return std::unexpected(payload.error());
  }
  return std::span<std::uint8_t>((*payload)->rgba.data(),
                                 (*payload)->rgba.size());
}

RenderTargetPool::RenderTargetPool(
    RenderTargetPoolConfig config,
    std::unique_ptr<GpuResourceRegistry> registry)
    : config_(config), registry_(std::move(registry)) {}

RenderTargetPool::RenderTargetPool(RenderTargetPool &&) noexcept = default;
RenderTargetPool &
RenderTargetPool::operator=(RenderTargetPool &&) noexcept = default;

RenderTargetPool::~RenderTargetPool() {
  if (registry_ == nullptr) {
    return;
  }
  for (GpuResourceHandle handle : free_list_) {
    (void)registry_->request_retire(handle);
  }
  free_list_.clear();
}

std::expected<RenderTargetPool, diagnostics::Error>
RenderTargetPool::create(RenderTargetPoolConfig config) {
  if (config.registry_capacity == 0) {
    return std::unexpected(
        make_invalid("invalid_pool_capacity", "registry_capacity must be > 0"));
  }
  if (config.max_pooled_bytes == 0 || config.max_allocation_bytes == 0) {
    return std::unexpected(
        make_invalid("invalid_pool_budget", "byte budgets must be > 0"));
  }
  auto registry = GpuResourceRegistry::create(config.registry_capacity);
  if (!registry) {
    return std::unexpected(registry.error());
  }
  return RenderTargetPool{
      config, std::make_unique<GpuResourceRegistry>(std::move(*registry))};
}

std::expected<std::uint64_t, diagnostics::Error>
RenderTargetPool::rgba_bytes(GpuResourceDesc desc) {
  if (desc.width_px == 0 || desc.height_px == 0) {
    return std::unexpected(make_invalid("invalid_resource_extent",
                                        "target width and height must be > 0"));
  }
  const std::uint64_t width = desc.width_px;
  const std::uint64_t height = desc.height_px;
  if (width > (std::numeric_limits<std::uint64_t>::max() / height)) {
    return std::unexpected(
        make_budget("target pixel count overflows budget arithmetic"));
  }
  const std::uint64_t pixels = width * height;
  if (pixels > (std::numeric_limits<std::uint64_t>::max() / 4U)) {
    return std::unexpected(
        make_budget("target byte size overflows budget arithmetic"));
  }
  return pixels * 4U;
}

void RenderTargetPool::note_bytes_delta(std::int64_t delta) noexcept {
  if (delta >= 0) {
    stats_.bytes_currently_pooled += static_cast<std::uint64_t>(delta);
  } else {
    const std::uint64_t sub = static_cast<std::uint64_t>(-delta);
    if (sub >= stats_.bytes_currently_pooled) {
      stats_.bytes_currently_pooled = 0;
    } else {
      stats_.bytes_currently_pooled -= sub;
    }
  }
  stats_.bytes_high_water =
      std::max(stats_.bytes_high_water, stats_.bytes_currently_pooled);
}

void RenderTargetPool::apply_clear(PooledTarget &target) noexcept {
  switch (config_.clear_policy) {
  case RenderTargetClearPolicy::clear_to_transparent:
    std::fill(target.rgba.begin(), target.rgba.end(),
              static_cast<std::uint8_t>(0));
    break;
  case RenderTargetClearPolicy::clear_to_color:
    for (std::size_t i = 0; i + 3 < target.rgba.size(); i += 4) {
      target.rgba[i] = config_.clear_color.r;
      target.rgba[i + 1] = config_.clear_color.g;
      target.rgba[i + 2] = config_.clear_color.b;
      target.rgba[i + 3] = config_.clear_color.a;
    }
    break;
  }
  ++stats_.clears_performed;
}

void RenderTargetPool::recycle(GpuResourceHandle handle,
                               GpuResourceDesc desc) noexcept {
  if (registry_ == nullptr || !handle.valid()) {
    return;
  }
  auto payload = registry_->try_get_as<PooledTarget>(handle);
  if (payload) {
    apply_clear(**payload);
  }
  try {
    free_list_.push_back(handle);
  } catch (...) {
    // Free-list growth failed: retire so we never leak a live handle.
    (void)registry_->request_retire(handle);
    auto bytes = rgba_bytes(desc);
    if (bytes) {
      note_bytes_delta(-static_cast<std::int64_t>(*bytes));
    }
    if (stats_.in_use_count > 0) {
      --stats_.in_use_count;
    }
    ++stats_.releases_total;
    return;
  }
  if (stats_.in_use_count > 0) {
    --stats_.in_use_count;
  }
  ++stats_.releases_total;
  stats_.free_list_size = static_cast<std::uint32_t>(free_list_.size());
}

std::expected<RenderTargetLease, diagnostics::Error>
RenderTargetPool::acquire(GpuResourceDesc desc) {
  auto needed = rgba_bytes(desc);
  if (!needed) {
    return std::unexpected(needed.error());
  }
  if (*needed > config_.max_allocation_bytes) {
    return std::unexpected(make_budget(
        "single target exceeds max_allocation_bytes; quality is not reduced"));
  }

  ++stats_.acquires_total;

  // Prefer an exact-size free target.
  for (std::size_t i = 0; i < free_list_.size(); ++i) {
    const GpuResourceHandle candidate = free_list_[i];
    auto candidate_desc = registry_->desc(candidate);
    if (!candidate_desc) {
      continue;
    }
    if (candidate_desc->width_px != desc.width_px ||
        candidate_desc->height_px != desc.height_px) {
      continue;
    }
    free_list_.erase(free_list_.begin() + static_cast<std::ptrdiff_t>(i));
    stats_.free_list_size = static_cast<std::uint32_t>(free_list_.size());

    auto payload = registry_->try_get_as<PooledTarget>(candidate);
    if (!payload) {
      return std::unexpected(payload.error());
    }
    // Clear again at acquire so a buggy release path cannot leak pixels.
    apply_clear(**payload);
    ++stats_.acquires_reused;
    ++stats_.in_use_count;
    return RenderTargetLease{this, candidate, desc};
  }

  // New allocation: preflight budget (no silent size downgrade).
  if (stats_.bytes_currently_pooled >
          std::numeric_limits<std::uint64_t>::max() - *needed ||
      stats_.bytes_currently_pooled + *needed > config_.max_pooled_bytes) {
    return std::unexpected(
        make_budget("pooled byte budget exceeded; quality is not reduced"));
  }

  std::unique_ptr<PooledTarget> target;
  try {
    target = std::make_unique<PooledTarget>();
    target->width_px = desc.width_px;
    target->height_px = desc.height_px;
    target->rgba.assign(static_cast<std::size_t>(*needed), 0);
  } catch (const std::bad_alloc &) {
    return std::unexpected(make_budget("cannot allocate render target pixels"));
  }

  apply_clear(*target);
  auto handle = registry_->create_resource(
      desc, std::unique_ptr<GpuResourcePayload>(std::move(target)));
  if (!handle) {
    return std::unexpected(handle.error());
  }
  if (auto upload = registry_->begin_upload(*handle); !upload) {
    (void)registry_->request_retire(*handle);
    return std::unexpected(upload.error());
  }
  if (auto resident = registry_->mark_resident(*handle); !resident) {
    (void)registry_->request_retire(*handle);
    return std::unexpected(resident.error());
  }

  note_bytes_delta(static_cast<std::int64_t>(*needed));
  ++stats_.allocations_created;
  ++stats_.in_use_count;
  return RenderTargetLease{this, *handle, desc};
}

} // namespace instantee::render_core
