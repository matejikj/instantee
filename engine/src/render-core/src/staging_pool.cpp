#include <instantee/render_core/staging_pool.hpp>

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
                            "staging_pool", message};
}

[[nodiscard]] diagnostics::Error make_would_block(std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::would_block,
                            diagnostics::ErrorKind::would_block, "would_block",
                            "staging_pool", message};
}

[[nodiscard]] diagnostics::Error make_budget(std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::resource_budget_exceeded,
                            diagnostics::ErrorKind::resource_budget_exceeded,
                            "resource_budget_exceeded", "staging_pool",
                            message};
}

[[nodiscard]] std::expected<std::size_t, diagnostics::Error>
rgba_bytes(std::uint32_t width_px, std::uint32_t height_px) {
  if (width_px == 0 || height_px == 0) {
    return std::unexpected(make_invalid(
        "invalid_staging_extent", "staging width and height must be > 0"));
  }
  const std::uint64_t width = width_px;
  const std::uint64_t height = height_px;
  if (width > (std::numeric_limits<std::uint64_t>::max() / height)) {
    return std::unexpected(make_budget("staging pixel count overflow"));
  }
  const std::uint64_t pixels = width * height;
  if (pixels > (std::numeric_limits<std::uint64_t>::max() / 4U)) {
    return std::unexpected(make_budget("staging byte size overflow"));
  }
  const std::uint64_t bytes = pixels * 4U;
  if (bytes >
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return std::unexpected(make_budget("staging byte size exceeds size_t"));
  }
  return static_cast<std::size_t>(bytes);
}

} // namespace

StagingBufferPool::Slot *
StagingBufferPool::slot_at(std::uint32_t index) noexcept {
  if (index >= slots_.size()) {
    return nullptr;
  }
  return &slots_[index];
}

const StagingBufferPool::Slot *
StagingBufferPool::slot_at(std::uint32_t index) const noexcept {
  if (index >= slots_.size()) {
    return nullptr;
  }
  return &slots_[index];
}

StagingLease::StagingLease(StagingBufferPool *pool,
                           std::uint32_t index) noexcept
    : pool_(pool), index_(index) {}

StagingLease::StagingLease(StagingLease &&other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)),
      index_(std::exchange(other.index_,
                           std::numeric_limits<std::uint32_t>::max())) {}

StagingLease &StagingLease::operator=(StagingLease &&other) noexcept {
  if (this != &other) {
    release();
    pool_ = std::exchange(other.pool_, nullptr);
    index_ =
        std::exchange(other.index_, std::numeric_limits<std::uint32_t>::max());
  }
  return *this;
}

StagingLease::~StagingLease() { release(); }

StagingLease::State StagingLease::state() const noexcept {
  if (empty()) {
    return State::consumed;
  }
  const StagingBufferPool::Slot *slot = pool_->slot_at(index_);
  if (slot == nullptr) {
    return State::consumed;
  }
  if (slot->cancelled) {
    return State::cancelled;
  }
  if (slot->gpu_hold && slot->sink_hold) {
    return State::gpu_and_sink_busy;
  }
  if (slot->gpu_hold) {
    return State::gpu_busy;
  }
  if (slot->sink_hold) {
    return State::sink_busy;
  }
  return State::writable;
}

std::uint32_t StagingLease::width_px() const noexcept {
  return pool_ == nullptr ? 0U : pool_->config_.width_px;
}

std::uint32_t StagingLease::height_px() const noexcept {
  return pool_ == nullptr ? 0U : pool_->config_.height_px;
}

std::size_t StagingLease::byte_size() const noexcept {
  if (empty()) {
    return 0;
  }
  const StagingBufferPool::Slot *slot = pool_->slot_at(index_);
  return slot == nullptr ? 0U : slot->rgba.size();
}

std::expected<std::span<std::uint8_t>, diagnostics::Error>
StagingLease::pixels() {
  if (empty()) {
    return std::unexpected(
        make_invalid("empty_staging_lease", "staging lease is empty"));
  }
  StagingBufferPool::Slot *slot = pool_->slot_at(index_);
  if (slot == nullptr) {
    return std::unexpected(
        make_invalid("empty_staging_lease", "staging lease is empty"));
  }
  if (slot->cancelled) {
    return std::unexpected(make_invalid(
        "staging_cancelled", "cancelled staging lease cannot expose pixels"));
  }
  if (slot->gpu_hold || slot->sink_hold) {
    return std::unexpected(make_invalid(
        "staging_busy", "staging pixels are frozen while GPU/sink holds"));
  }
  return std::span<std::uint8_t>(slot->rgba.data(), slot->rgba.size());
}

std::expected<std::span<const std::uint8_t>, diagnostics::Error>
StagingLease::frozen_bytes() const {
  if (empty()) {
    return std::unexpected(
        make_invalid("empty_staging_lease", "staging lease is empty"));
  }
  const StagingBufferPool::Slot *slot = pool_->slot_at(index_);
  if (slot == nullptr) {
    return std::unexpected(
        make_invalid("empty_staging_lease", "staging lease is empty"));
  }
  return std::span<const std::uint8_t>(slot->rgba.data(), slot->rgba.size());
}

std::expected<void, diagnostics::Error>
StagingLease::fill_rgba8(std::span<const std::uint8_t> tightly_packed) {
  auto view = pixels();
  if (!view) {
    return std::unexpected(view.error());
  }
  if (tightly_packed.size() != view->size()) {
    return std::unexpected(make_invalid(
        "size_mismatch", "RGBA8 payload size does not match staging buffer"));
  }
  std::copy(tightly_packed.begin(), tightly_packed.end(), view->begin());
  return {};
}

std::expected<void, diagnostics::Error> StagingLease::mark_gpu_busy() {
  if (empty()) {
    return std::unexpected(
        make_invalid("empty_staging_lease", "staging lease is empty"));
  }
  StagingBufferPool::Slot *slot = pool_->slot_at(index_);
  if (slot == nullptr) {
    return std::unexpected(
        make_invalid("empty_staging_lease", "staging lease is empty"));
  }
  if (slot->cancelled) {
    return std::unexpected(make_invalid("staging_cancelled",
                                        "cancelled lease cannot go GPU busy"));
  }
  if (slot->gpu_hold) {
    return std::unexpected(
        make_invalid("staging_gpu_hold", "GPU hold is already active"));
  }
  slot->gpu_hold = true;
  return {};
}

std::expected<void, diagnostics::Error> StagingLease::complete_gpu() {
  if (empty()) {
    return std::unexpected(
        make_invalid("empty_staging_lease", "staging lease is empty"));
  }
  StagingBufferPool::Slot *slot = pool_->slot_at(index_);
  if (slot == nullptr || !slot->gpu_hold) {
    return std::unexpected(
        make_invalid("staging_gpu_hold", "no active GPU hold to complete"));
  }
  slot->gpu_hold = false;
  recycle_if_idle();
  return {};
}

std::expected<void, diagnostics::Error> StagingLease::mark_sink_busy() {
  if (empty()) {
    return std::unexpected(
        make_invalid("empty_staging_lease", "staging lease is empty"));
  }
  StagingBufferPool::Slot *slot = pool_->slot_at(index_);
  if (slot == nullptr) {
    return std::unexpected(
        make_invalid("empty_staging_lease", "staging lease is empty"));
  }
  if (slot->cancelled) {
    return std::unexpected(make_invalid("staging_cancelled",
                                        "cancelled lease cannot go sink busy"));
  }
  if (slot->sink_hold) {
    return std::unexpected(
        make_invalid("staging_sink_hold", "sink hold is already active"));
  }
  slot->sink_hold = true;
  return {};
}

std::expected<void, diagnostics::Error> StagingLease::complete_sink() {
  if (empty()) {
    return std::unexpected(
        make_invalid("empty_staging_lease", "staging lease is empty"));
  }
  StagingBufferPool::Slot *slot = pool_->slot_at(index_);
  if (slot == nullptr || !slot->sink_hold) {
    return std::unexpected(
        make_invalid("staging_sink_hold", "no active sink hold to complete"));
  }
  slot->sink_hold = false;
  recycle_if_idle();
  return {};
}

void StagingLease::cancel() noexcept {
  if (empty()) {
    return;
  }
  if (StagingBufferPool::Slot *slot = pool_->slot_at(index_); slot != nullptr) {
    slot->cancelled = true;
  }
  recycle_if_idle();
}

void StagingLease::recycle_if_idle() noexcept {
  if (empty()) {
    return;
  }
  StagingBufferPool::Slot *slot = pool_->slot_at(index_);
  if (slot == nullptr) {
    pool_ = nullptr;
    index_ = std::numeric_limits<std::uint32_t>::max();
    return;
  }
  if (slot->gpu_hold || slot->sink_hold) {
    return;
  }
  std::fill(slot->rgba.begin(), slot->rgba.end(), static_cast<std::uint8_t>(0));
  slot->cancelled = false;
  slot->checked_out = false;
  pool_->note_recycle();
  pool_ = nullptr;
  index_ = std::numeric_limits<std::uint32_t>::max();
}

void StagingLease::release() noexcept {
  if (empty()) {
    pool_ = nullptr;
    index_ = std::numeric_limits<std::uint32_t>::max();
    return;
  }
  if (StagingBufferPool::Slot *slot = pool_->slot_at(index_); slot != nullptr) {
    slot->gpu_hold = false;
    slot->sink_hold = false;
    slot->cancelled = true;
  }
  recycle_if_idle();
}

StagingBufferPool::StagingBufferPool(StagingPoolConfig config,
                                     std::vector<Slot> slots)
    : config_(config), slots_(std::move(slots)) {
  stats_.capacity = config_.capacity;
  stats_.free_count = config_.capacity;
}

StagingBufferPool::StagingBufferPool(StagingBufferPool &&) noexcept = default;
StagingBufferPool &
StagingBufferPool::operator=(StagingBufferPool &&) noexcept = default;
StagingBufferPool::~StagingBufferPool() = default;

std::expected<StagingBufferPool, diagnostics::Error>
StagingBufferPool::create(StagingPoolConfig config) {
  if (config.capacity == 0) {
    return std::unexpected(
        make_invalid("invalid_staging_capacity", "capacity must be > 0"));
  }
  auto bytes = rgba_bytes(config.width_px, config.height_px);
  if (!bytes) {
    return std::unexpected(bytes.error());
  }

  std::vector<Slot> slots;
  try {
    slots.resize(config.capacity);
    for (Slot &slot : slots) {
      slot.rgba.assign(*bytes, 0);
    }
  } catch (const std::bad_alloc &) {
    return std::unexpected(make_budget("cannot allocate staging pool buffers"));
  }
  return StagingBufferPool{config, std::move(slots)};
}

StagingPoolStats StagingBufferPool::stats() const noexcept {
  StagingPoolStats out = stats_;
  out.capacity = config_.capacity;
  std::uint32_t in_use = 0;
  for (const Slot &slot : slots_) {
    if (slot.checked_out) {
      ++in_use;
    }
  }
  out.in_use = in_use;
  out.free_count = config_.capacity - in_use;
  return out;
}

void StagingBufferPool::note_recycle() noexcept { ++stats_.recycles; }

std::expected<StagingLease, diagnostics::Error>
StagingBufferPool::try_acquire() {
  ++stats_.acquires;
  for (std::uint32_t i = 0; i < slots_.size(); ++i) {
    Slot &slot = slots_[i];
    if (!slot.checked_out) {
      slot.checked_out = true;
      slot.gpu_hold = false;
      slot.sink_hold = false;
      slot.cancelled = false;
      return StagingLease{this, i};
    }
  }
  ++stats_.acquire_rejections;
  return std::unexpected(make_would_block(
      "staging pool allocation threshold reached; no fallback allocation"));
}

std::expected<void, diagnostics::Error>
copy_staging_to_sink(std::span<const std::uint8_t> staging_rgba,
                     std::span<std::uint8_t> sink_rgba) {
  if (staging_rgba.size() != sink_rgba.size()) {
    return std::unexpected(make_invalid(
        "size_mismatch", "staging and sink RGBA8 sizes do not match"));
  }
  std::copy(staging_rgba.begin(), staging_rgba.end(), sink_rgba.begin());
  return {};
}

} // namespace instantee::render_core
