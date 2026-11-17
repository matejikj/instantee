#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace instantee::render_core {

// Opaque generational id. Zero is invalid / unbound (matches PassDesc).
// Layout: high 32 bits = generation (never zero when valid), low 32 bits =
// 1-based slot index.
struct GpuResourceHandle {
  std::uint64_t value = 0;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return valid();
  }
  [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return value; }

  [[nodiscard]] static constexpr GpuResourceHandle
  from_raw(std::uint64_t raw) noexcept {
    return GpuResourceHandle{raw};
  }

  friend constexpr bool operator==(GpuResourceHandle,
                                   GpuResourceHandle) noexcept = default;
};

// Lifecycle from SAS/CPP contracts. Failed is terminal: the slot is not
// recycled as a successful resident resource.
enum class GpuResourceState : std::uint8_t {
  unloaded = 0,
  cpu_ready = 1,
  gpu_uploading = 2,
  gpu_resident = 3,
  retiring = 4,
  released = 5,
  failed = 6,
};

struct GpuResourceDesc {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
};

// Type-erased owning payload. Backends subclass; tests use instrumented types.
// Destruction runs exactly once when the registry releases the slot.
struct GpuResourcePayload {
  GpuResourcePayload() = default;
  GpuResourcePayload(const GpuResourcePayload &) = delete;
  GpuResourcePayload &operator=(const GpuResourcePayload &) = delete;
  GpuResourcePayload(GpuResourcePayload &&) = delete;
  GpuResourcePayload &operator=(GpuResourcePayload &&) = delete;
  virtual ~GpuResourcePayload() = default;
};

class GpuResourceRegistry;

// Move-only submission scope. Retained handles keep payloads alive until
// complete() or cancel(); the destructor cancels if neither was called.
class SubmissionContext {
public:
  SubmissionContext(SubmissionContext &&other) noexcept;
  SubmissionContext &operator=(SubmissionContext &&other) noexcept;
  ~SubmissionContext();

  SubmissionContext(const SubmissionContext &) = delete;
  SubmissionContext &operator=(const SubmissionContext &) = delete;

  // Pins a live (uploading/resident) resource for this submission. Stale or
  // retired handles fail without mutating the registry.
  [[nodiscard]] std::expected<void, diagnostics::Error>
  retain(GpuResourceHandle handle);

  // Releases all holds. Resources marked retiring with zero holds are freed.
  [[nodiscard]] std::expected<void, diagnostics::Error> complete();

  // Same lifetime release as complete(); records cancellation for callers.
  [[nodiscard]] std::expected<void, diagnostics::Error> cancel();

  [[nodiscard]] bool finished() const noexcept { return finished_; }
  [[nodiscard]] std::size_t retained_count() const noexcept {
    return retained_.size();
  }

private:
  friend class GpuResourceRegistry;
  explicit SubmissionContext(GpuResourceRegistry *registry) noexcept;

  void release_holds() noexcept;

  GpuResourceRegistry *registry_ = nullptr;
  std::vector<GpuResourceHandle> retained_{};
  bool finished_ = false;
};

// Fixed-capacity RAII registry. Slots bump generation on release so stale
// handles cannot revive a recycled index. Retirement is bounded by capacity:
// a resource with outstanding submission holds stays in `retiring` until the
// last hold drops, then the payload is destroyed once.
class GpuResourceRegistry {
public:
  static constexpr std::uint32_t kDefaultCapacity = 8;

  GpuResourceRegistry(GpuResourceRegistry &&) noexcept;
  GpuResourceRegistry &operator=(GpuResourceRegistry &&) noexcept;
  ~GpuResourceRegistry();

  GpuResourceRegistry(const GpuResourceRegistry &) = delete;
  GpuResourceRegistry &operator=(const GpuResourceRegistry &) = delete;

  [[nodiscard]] static std::expected<GpuResourceRegistry, diagnostics::Error>
  create(std::uint32_t capacity = kDefaultCapacity);

  [[nodiscard]] std::uint32_t capacity() const noexcept;
  [[nodiscard]] std::uint32_t live_count() const noexcept;
  [[nodiscard]] std::uint32_t retiring_count() const noexcept;

  // Allocates a slot, installs payload, transitions unloaded → cpu_ready.
  // On failure no handle is published and no payload is retained.
  [[nodiscard]] std::expected<GpuResourceHandle, diagnostics::Error>
  create_resource(GpuResourceDesc desc,
                  std::unique_ptr<GpuResourcePayload> payload);

  template <typename T, typename... Args>
  [[nodiscard]] std::expected<GpuResourceHandle, diagnostics::Error>
  emplace_resource(GpuResourceDesc desc, Args &&...args) {
    static_assert(std::is_base_of_v<GpuResourcePayload, T>);
    std::unique_ptr<GpuResourcePayload> payload;
    try {
      payload = std::make_unique<T>(std::forward<Args>(args)...);
    } catch (const std::bad_alloc &) {
      return std::unexpected(
          diagnostics::Error{diagnostics::ExitCode::resource_budget_exceeded,
                             diagnostics::ErrorKind::resource_budget_exceeded,
                             "resource_budget_exceeded", "resource_registry",
                             "payload allocation failed"});
    }
    return create_resource(std::move(desc), std::move(payload));
  }

  [[nodiscard]] std::expected<void, diagnostics::Error>
  begin_upload(GpuResourceHandle handle);
  [[nodiscard]] std::expected<void, diagnostics::Error>
  mark_resident(GpuResourceHandle handle);

  // Marks the resource for retirement. Destroys immediately when no submission
  // holds remain; otherwise state becomes retiring until the last hold drops.
  [[nodiscard]] std::expected<void, diagnostics::Error>
  request_retire(GpuResourceHandle handle);

  // Fault injection: leave a slot half-initialized is impossible by API; this
  // marks an existing live resource failed and destroys it only after holds
  // drain (same as retire), rejecting further retain/upload.
  [[nodiscard]] std::expected<void, diagnostics::Error>
  mark_failed(GpuResourceHandle handle);

  [[nodiscard]] std::expected<GpuResourceState, diagnostics::Error>
  state(GpuResourceHandle handle) const;
  [[nodiscard]] std::expected<GpuResourceDesc, diagnostics::Error>
  desc(GpuResourceHandle handle) const;

  // Borrowed payload view; invalid after retire/release of the handle.
  [[nodiscard]] std::expected<GpuResourcePayload *, diagnostics::Error>
  try_get(GpuResourceHandle handle);

  template <typename T>
  [[nodiscard]] std::expected<T *, diagnostics::Error>
  try_get_as(GpuResourceHandle handle) {
    auto base = try_get(handle);
    if (!base) {
      return std::unexpected(base.error());
    }
    auto *typed = dynamic_cast<T *>(*base);
    if (typed == nullptr) {
      return std::unexpected(diagnostics::Error{
          diagnostics::ExitCode::invalid_input,
          diagnostics::ErrorKind::invalid_input, "resource_type_mismatch",
          "resource_registry", "payload type does not match request"});
    }
    return typed;
  }

  [[nodiscard]] SubmissionContext begin_submission();

private:
  friend class SubmissionContext;

  struct Slot {
    std::uint32_t generation = 1;
    GpuResourceState state = GpuResourceState::released;
    GpuResourceDesc desc{};
    std::unique_ptr<GpuResourcePayload> payload{};
    std::uint32_t submission_holds = 0;
  };

  explicit GpuResourceRegistry(std::vector<Slot> slots) noexcept;

  [[nodiscard]] std::expected<std::size_t, diagnostics::Error>
  resolve_index(GpuResourceHandle handle) const;
  [[nodiscard]] static GpuResourceHandle make_handle(std::uint32_t index,
                                                     std::uint32_t generation);
  void release_slot(std::size_t index) noexcept;
  void drop_hold(GpuResourceHandle handle) noexcept;

  std::vector<Slot> slots_{};
  std::uint32_t live_count_ = 0;
  std::uint32_t retiring_count_ = 0;
};

} // namespace instantee::render_core
