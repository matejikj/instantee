#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/pipeline_metrics.hpp>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>
#include <utility>
#include <vector>

namespace instantee::runtime {

// Fixed-capacity slot table for decoding out-of-order frame indices back into
// contiguous presentation order. Does not block; callers apply backpressure via
// frame credits / queues instead of dropping offline frames.
template <typename T> class ReorderBuffer {
public:
  ReorderBuffer(const ReorderBuffer &) = delete;
  ReorderBuffer &operator=(const ReorderBuffer &) = delete;
  ReorderBuffer(ReorderBuffer &&) noexcept = default;
  ReorderBuffer &operator=(ReorderBuffer &&) noexcept = default;

  [[nodiscard]] static std::expected<ReorderBuffer, spike::Error>
  create(std::uint32_t capacity);

  [[nodiscard]] std::uint32_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] std::uint64_t next_index() const noexcept {
    return next_index_;
  }
  [[nodiscard]] std::uint32_t occupied() const noexcept { return occupied_; }
  [[nodiscard]] PipelineMetrics metrics() const noexcept { return metrics_; }

  // Stores value for absolute frame index. Rejects duplicates, already-released
  // indices, and overflow beyond the open window [next_index, next_index+cap).
  [[nodiscard]] std::expected<void, spike::Error> insert(std::uint64_t index,
                                                         T value);

  // Removes and returns the next contiguous frame when present.
  [[nodiscard]] std::optional<T> try_pop_next();

private:
  static constexpr const char *kPhase = "reorder_buffer";

  explicit ReorderBuffer(std::uint32_t capacity)
      : capacity_(capacity), slots_(capacity) {}

  struct Slot {
    bool occupied = false;
    T value{};
  };

  std::uint32_t capacity_ = 0;
  std::uint64_t next_index_ = 0;
  std::uint32_t occupied_ = 0;
  std::vector<Slot> slots_;
  PipelineMetrics metrics_{};
};

template <typename T>
std::expected<ReorderBuffer<T>, spike::Error>
ReorderBuffer<T>::create(std::uint32_t capacity) {
  if (capacity == 0U) {
    return std::unexpected(spike::Error{spike::ExitCode::invalid_input,
                                        "invalid_reorder_capacity", kPhase,
                                        "Reorder buffer capacity must be "
                                        "non-zero"});
  }
  return ReorderBuffer{capacity};
}

template <typename T>
std::expected<void, spike::Error> ReorderBuffer<T>::insert(std::uint64_t index,
                                                           T value) {
  if (index < next_index_) {
    return std::unexpected(spike::Error{spike::ExitCode::invalid_input,
                                        "stale_frame_index", kPhase,
                                        "Frame index was already released"});
  }
  const std::uint64_t offset = index - next_index_;
  if (offset >= capacity_) {
    return std::unexpected(spike::Error{spike::ExitCode::invalid_input,
                                        "reorder_window_full", kPhase,
                                        "Frame index is outside the open "
                                        "reorder window"});
  }
  Slot &slot = slots_[static_cast<std::size_t>(offset)];
  if (slot.occupied) {
    return std::unexpected(spike::Error{spike::ExitCode::invalid_input,
                                        "duplicate_frame_index", kPhase,
                                        "Frame index is already buffered"});
  }
  slot.occupied = true;
  slot.value = std::move(value);
  ++occupied_;
  if (occupied_ > metrics_.high_water) {
    metrics_.high_water = occupied_;
  }
  return {};
}

template <typename T> std::optional<T> ReorderBuffer<T>::try_pop_next() {
  if (slots_.empty() || !slots_.front().occupied) {
    return std::nullopt;
  }
  T value = std::move(slots_.front().value);
  slots_.front().occupied = false;
  // Rotate window left by one without reallocating the fixed capacity.
  std::rotate(slots_.begin(), slots_.begin() + 1, slots_.end());
  --occupied_;
  ++next_index_;
  return value;
}

} // namespace instantee::runtime
