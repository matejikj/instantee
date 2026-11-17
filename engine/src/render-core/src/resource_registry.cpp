#include <instantee/render_core/resource_registry.hpp>

#include <limits>
#include <string_view>
#include <utility>

namespace instantee::render_core {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "resource_registry", message};
}

[[nodiscard]] diagnostics::Error make_budget(std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::resource_budget_exceeded,
                            diagnostics::ErrorKind::resource_budget_exceeded,
                            "resource_budget_exceeded", "resource_registry",
                            message};
}

[[nodiscard]] bool is_retainable(GpuResourceState state) noexcept {
  return state == GpuResourceState::gpu_uploading ||
         state == GpuResourceState::gpu_resident;
}

[[nodiscard]] bool is_live_occupying(GpuResourceState state) noexcept {
  return state == GpuResourceState::unloaded ||
         state == GpuResourceState::cpu_ready ||
         state == GpuResourceState::gpu_uploading ||
         state == GpuResourceState::gpu_resident ||
         state == GpuResourceState::retiring ||
         state == GpuResourceState::failed;
}

} // namespace

GpuResourceHandle GpuResourceRegistry::make_handle(std::uint32_t index,
                                                   std::uint32_t generation) {
  return GpuResourceHandle{(static_cast<std::uint64_t>(generation) << 32U) |
                           (static_cast<std::uint64_t>(index) + 1U)};
}

GpuResourceRegistry::GpuResourceRegistry(std::vector<Slot> slots) noexcept
    : slots_(std::move(slots)) {}

GpuResourceRegistry::GpuResourceRegistry(GpuResourceRegistry &&) noexcept =
    default;
GpuResourceRegistry &
GpuResourceRegistry::operator=(GpuResourceRegistry &&) noexcept = default;

GpuResourceRegistry::~GpuResourceRegistry() {
  // Drop all payloads. SubmissionContexts must not outlive the registry; any
  // remaining holds are cleared so destructors still run once.
  for (Slot &slot : slots_) {
    slot.submission_holds = 0;
    slot.payload.reset();
    slot.state = GpuResourceState::released;
  }
  live_count_ = 0;
  retiring_count_ = 0;
}

std::expected<GpuResourceRegistry, diagnostics::Error>
GpuResourceRegistry::create(std::uint32_t capacity) {
  if (capacity == 0) {
    return std::unexpected(
        make_invalid("invalid_registry_capacity", "capacity must be > 0"));
  }
  std::vector<Slot> slots;
  try {
    slots.resize(capacity);
  } catch (const std::bad_alloc &) {
    return std::unexpected(make_budget("cannot allocate registry slots"));
  }
  for (std::uint32_t i = 0; i < capacity; ++i) {
    slots[i].generation = 1;
    slots[i].state = GpuResourceState::released;
  }
  return GpuResourceRegistry{std::move(slots)};
}

std::uint32_t GpuResourceRegistry::capacity() const noexcept {
  return static_cast<std::uint32_t>(slots_.size());
}

std::uint32_t GpuResourceRegistry::live_count() const noexcept {
  return live_count_;
}

std::uint32_t GpuResourceRegistry::retiring_count() const noexcept {
  return retiring_count_;
}

std::expected<std::size_t, diagnostics::Error>
GpuResourceRegistry::resolve_index(GpuResourceHandle handle) const {
  if (!handle.valid()) {
    return std::unexpected(
        make_invalid("invalid_resource_handle", "resource handle is unbound"));
  }
  const std::uint32_t packed_index =
      static_cast<std::uint32_t>(handle.value & 0xffffffffU);
  const std::uint32_t generation =
      static_cast<std::uint32_t>(handle.value >> 32U);
  if (packed_index == 0 || generation == 0) {
    return std::unexpected(make_invalid("invalid_resource_handle",
                                        "resource handle is malformed"));
  }
  const std::size_t index = static_cast<std::size_t>(packed_index - 1U);
  if (index >= slots_.size()) {
    return std::unexpected(make_invalid("stale_resource_handle",
                                        "resource handle index is stale"));
  }
  const Slot &slot = slots_[index];
  if (slot.generation != generation ||
      slot.state == GpuResourceState::released) {
    return std::unexpected(
        make_invalid("stale_resource_handle",
                     "resource handle generation does not match live slot"));
  }
  return index;
}

void GpuResourceRegistry::release_slot(std::size_t index) noexcept {
  Slot &slot = slots_[index];
  if (slot.state == GpuResourceState::retiring && retiring_count_ > 0) {
    --retiring_count_;
  }
  if (is_live_occupying(slot.state) && live_count_ > 0) {
    --live_count_;
  }
  slot.payload.reset();
  slot.desc = {};
  slot.submission_holds = 0;
  if (slot.generation < std::numeric_limits<std::uint32_t>::max()) {
    ++slot.generation;
  } else {
    slot.generation = 1;
  }
  slot.state = GpuResourceState::released;
}

void GpuResourceRegistry::drop_hold(GpuResourceHandle handle) noexcept {
  auto index = resolve_index(handle);
  if (!index) {
    return;
  }
  Slot &slot = slots_[*index];
  if (slot.submission_holds == 0) {
    return;
  }
  --slot.submission_holds;
  if (slot.submission_holds == 0 && (slot.state == GpuResourceState::retiring ||
                                     slot.state == GpuResourceState::failed)) {
    release_slot(*index);
  }
}

std::expected<GpuResourceHandle, diagnostics::Error>
GpuResourceRegistry::create_resource(
    GpuResourceDesc desc, std::unique_ptr<GpuResourcePayload> payload) {
  if (!payload) {
    return std::unexpected(
        make_invalid("null_resource_payload", "resource payload is required"));
  }
  if (desc.width_px == 0 || desc.height_px == 0) {
    return std::unexpected(make_invalid(
        "invalid_resource_extent", "resource width and height must be > 0"));
  }

  std::size_t free_index = slots_.size();
  for (std::size_t i = 0; i < slots_.size(); ++i) {
    if (slots_[i].state == GpuResourceState::released) {
      free_index = i;
      break;
    }
  }
  if (free_index == slots_.size()) {
    return std::unexpected(make_budget("resource registry capacity exhausted"));
  }

  Slot &slot = slots_[free_index];
  // Publish only after payload is installed: observers never see a half-init
  // live handle if construction already succeeded into `payload`.
  slot.payload = std::move(payload);
  slot.desc = desc;
  slot.submission_holds = 0;
  slot.state = GpuResourceState::cpu_ready;
  ++live_count_;
  return make_handle(static_cast<std::uint32_t>(free_index), slot.generation);
}

std::expected<void, diagnostics::Error>
GpuResourceRegistry::begin_upload(GpuResourceHandle handle) {
  auto index = resolve_index(handle);
  if (!index) {
    return std::unexpected(index.error());
  }
  Slot &slot = slots_[*index];
  if (slot.state != GpuResourceState::cpu_ready) {
    return std::unexpected(make_invalid("invalid_resource_state",
                                        "begin_upload requires cpu_ready"));
  }
  slot.state = GpuResourceState::gpu_uploading;
  return {};
}

std::expected<void, diagnostics::Error>
GpuResourceRegistry::mark_resident(GpuResourceHandle handle) {
  auto index = resolve_index(handle);
  if (!index) {
    return std::unexpected(index.error());
  }
  Slot &slot = slots_[*index];
  if (slot.state != GpuResourceState::gpu_uploading) {
    return std::unexpected(make_invalid(
        "invalid_resource_state", "mark_resident requires gpu_uploading"));
  }
  slot.state = GpuResourceState::gpu_resident;
  return {};
}

std::expected<void, diagnostics::Error>
GpuResourceRegistry::request_retire(GpuResourceHandle handle) {
  auto index = resolve_index(handle);
  if (!index) {
    return std::unexpected(index.error());
  }
  Slot &slot = slots_[*index];
  if (slot.state == GpuResourceState::retiring) {
    return {};
  }
  if (slot.state == GpuResourceState::failed) {
    if (slot.submission_holds == 0) {
      release_slot(*index);
    }
    return {};
  }
  if (slot.state == GpuResourceState::released) {
    return std::unexpected(
        make_invalid("stale_resource_handle", "resource already released"));
  }
  if (slot.submission_holds == 0) {
    release_slot(*index);
    return {};
  }
  slot.state = GpuResourceState::retiring;
  ++retiring_count_;
  return {};
}

std::expected<void, diagnostics::Error>
GpuResourceRegistry::mark_failed(GpuResourceHandle handle) {
  auto index = resolve_index(handle);
  if (!index) {
    return std::unexpected(index.error());
  }
  Slot &slot = slots_[*index];
  if (slot.state == GpuResourceState::released) {
    return std::unexpected(
        make_invalid("stale_resource_handle", "resource already released"));
  }
  if (slot.state == GpuResourceState::retiring) {
    --retiring_count_;
  }
  slot.state = GpuResourceState::failed;
  if (slot.submission_holds == 0) {
    release_slot(*index);
  }
  return {};
}

std::expected<GpuResourceState, diagnostics::Error>
GpuResourceRegistry::state(GpuResourceHandle handle) const {
  auto index = resolve_index(handle);
  if (!index) {
    return std::unexpected(index.error());
  }
  return slots_[*index].state;
}

std::expected<GpuResourceDesc, diagnostics::Error>
GpuResourceRegistry::desc(GpuResourceHandle handle) const {
  auto index = resolve_index(handle);
  if (!index) {
    return std::unexpected(index.error());
  }
  return slots_[*index].desc;
}

std::expected<GpuResourcePayload *, diagnostics::Error>
GpuResourceRegistry::try_get(GpuResourceHandle handle) {
  auto index = resolve_index(handle);
  if (!index) {
    return std::unexpected(index.error());
  }
  Slot &slot = slots_[*index];
  if (slot.state == GpuResourceState::failed ||
      slot.state == GpuResourceState::retiring) {
    // Still alive until holds drain, but callers must not treat as resident.
    if (slot.payload == nullptr) {
      return std::unexpected(
          make_invalid("stale_resource_handle", "resource payload missing"));
    }
    return slot.payload.get();
  }
  if (!is_retainable(slot.state) && slot.state != GpuResourceState::cpu_ready &&
      slot.state != GpuResourceState::unloaded) {
    return std::unexpected(make_invalid(
        "invalid_resource_state", "resource is not available for access"));
  }
  if (slot.payload == nullptr) {
    return std::unexpected(
        make_invalid("stale_resource_handle", "resource payload missing"));
  }
  return slot.payload.get();
}

SubmissionContext GpuResourceRegistry::begin_submission() {
  return SubmissionContext{this};
}

SubmissionContext::SubmissionContext(GpuResourceRegistry *registry) noexcept
    : registry_(registry) {}

SubmissionContext::SubmissionContext(SubmissionContext &&other) noexcept
    : registry_(std::exchange(other.registry_, nullptr)),
      retained_(std::move(other.retained_)), finished_(other.finished_) {
  other.finished_ = true;
  other.retained_.clear();
}

SubmissionContext &
SubmissionContext::operator=(SubmissionContext &&other) noexcept {
  if (this != &other) {
    if (!finished_) {
      release_holds();
      finished_ = true;
    }
    registry_ = std::exchange(other.registry_, nullptr);
    retained_ = std::move(other.retained_);
    finished_ = other.finished_;
    other.finished_ = true;
    other.retained_.clear();
  }
  return *this;
}

SubmissionContext::~SubmissionContext() {
  if (!finished_) {
    release_holds();
    finished_ = true;
  }
}

void SubmissionContext::release_holds() noexcept {
  if (registry_ == nullptr) {
    retained_.clear();
    return;
  }
  for (GpuResourceHandle handle : retained_) {
    registry_->drop_hold(handle);
  }
  retained_.clear();
}

std::expected<void, diagnostics::Error>
SubmissionContext::retain(GpuResourceHandle handle) {
  if (finished_ || registry_ == nullptr) {
    return std::unexpected(make_invalid(
        "submission_finished", "submission context is already finished"));
  }
  auto index = registry_->resolve_index(handle);
  if (!index) {
    return std::unexpected(index.error());
  }
  GpuResourceRegistry::Slot &slot = registry_->slots_[*index];
  if (!is_retainable(slot.state)) {
    return std::unexpected(make_invalid(
        "invalid_resource_state",
        "submission can retain only uploading or resident resources"));
  }
  if (slot.submission_holds == std::numeric_limits<std::uint32_t>::max()) {
    return std::unexpected(make_budget("submission hold counter saturated"));
  }
  ++slot.submission_holds;
  try {
    retained_.push_back(handle);
  } catch (const std::bad_alloc &) {
    --slot.submission_holds;
    return std::unexpected(make_budget("cannot record submission retain"));
  }
  return {};
}

std::expected<void, diagnostics::Error> SubmissionContext::complete() {
  if (finished_) {
    return std::unexpected(make_invalid(
        "submission_finished", "submission context is already finished"));
  }
  release_holds();
  finished_ = true;
  return {};
}

std::expected<void, diagnostics::Error> SubmissionContext::cancel() {
  if (finished_) {
    return std::unexpected(make_invalid(
        "submission_finished", "submission context is already finished"));
  }
  release_holds();
  finished_ = true;
  return {};
}

} // namespace instantee::render_core
