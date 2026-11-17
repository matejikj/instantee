#include <instantee/render_metal/metal_backend.hpp>

#include "device_native.hpp"
#include "metal_owner.hpp"

#include <atomic>
#include <cstdint>
#include <string_view>
#include <utility>

namespace instantee::render_metal {
namespace {

[[nodiscard]] diagnostics::Error gpu_error(std::string_view code,
                                           std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::gpu_failure,
                            diagnostics::ErrorKind::gpu_failure, code, "metal",
                            message};
}

} // namespace

struct MetalSubmitOwner::Impl {
  Device device;
  std::atomic_flag submitting = ATOMIC_FLAG_INIT;
  std::uint64_t submit_count = 0;
};

MetalSubmitOwner::MetalSubmitOwner(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

MetalSubmitOwner::MetalSubmitOwner(MetalSubmitOwner &&) noexcept = default;
MetalSubmitOwner &
MetalSubmitOwner::operator=(MetalSubmitOwner &&) noexcept = default;
MetalSubmitOwner::~MetalSubmitOwner() = default;

std::expected<MetalSubmitOwner, diagnostics::Error>
MetalSubmitOwner::create(Device device) {
  auto alive = device.ensure_alive();
  if (!alive) {
    return std::unexpected(alive.error());
  }
  auto impl = std::unique_ptr<Impl>(new Impl{std::move(device)});
  return MetalSubmitOwner{std::move(impl)};
}

const Device &MetalSubmitOwner::device() const noexcept {
  return impl_->device;
}

Device &MetalSubmitOwner::device() noexcept { return impl_->device; }

std::uint64_t MetalSubmitOwner::submit_count() const noexcept {
  return impl_->submit_count;
}

std::expected<void, diagnostics::Error>
MetalSubmitOwner::submit(const render_core::PassDesc &pass) {
  if (impl_ == nullptr) {
    return std::unexpected(
        gpu_error("submit_owner_empty", "MetalSubmitOwner has no device"));
  }
  if (impl_->submitting.test_and_set(std::memory_order_acq_rel)) {
    return std::unexpected(
        gpu_error("concurrent_submit",
                  "Only one Metal submit may run on this owner at a time"));
  }
  struct ClearFlag {
    std::atomic_flag &flag;
    ~ClearFlag() { flag.clear(std::memory_order_release); }
  } clear{impl_->submitting};

  auto alive = impl_->device.ensure_alive();
  if (!alive) {
    return std::unexpected(alive.error());
  }

  auto valid = render_core::validate_pass(pass);
  if (!valid) {
    return valid;
  }

  detail::MetalOwner<NS::AutoreleasePool> pool{
      NS::AutoreleasePool::alloc()->init()};
  if (!pool) {
    return std::unexpected(
        gpu_error("autorelease_pool_failed",
                  "Cannot create Metal autorelease pool for submit"));
  }

  detail::DeviceNative native = detail::native_handles(impl_->device);
  if (native.device == nullptr || native.queue == nullptr) {
    return std::unexpected(
        gpu_error("metal_device_lost", "Metal native handles are unavailable"));
  }

  // Handshake: prove the single submit owner can create and commit a command
  // buffer. Full draw encoding waits on the resource registry.
  // commandBuffer() is autoreleased; retain before the local pool drains.
  MTL::CommandBuffer *raw_command = native.queue->commandBuffer();
  if (raw_command == nullptr) {
    return std::unexpected(gpu_error("command_buffer_unavailable",
                                     "Cannot create a Metal command buffer"));
  }
  detail::MetalOwner<MTL::CommandBuffer> command =
      detail::retain_owner(raw_command);
  command->commit();
  command->waitUntilCompleted();
  if (command->status() == MTL::CommandBufferStatusError) {
    return std::unexpected(gpu_error(
        "command_buffer_failed", "Metal command buffer completed with error"));
  }

  if (impl_->submit_count < UINT64_MAX) {
    ++impl_->submit_count;
  }
  return {};
}

} // namespace instantee::render_metal
