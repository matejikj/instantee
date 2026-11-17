#include <instantee/render_metal/device.hpp>

#include "device_native.hpp"
#include "metal_owner.hpp"

#include <atomic>
#include <string_view>
#include <utility>

namespace instantee::render_metal {
namespace {

[[nodiscard]] std::string copy_utf8(const NS::String *value) {
  if (value == nullptr) {
    return {};
  }
  const char *utf8 = value->utf8String();
  return utf8 == nullptr ? std::string{} : std::string{utf8};
}

[[nodiscard]] Error gpu_error(std::string_view code, std::string_view message) {
  return Error{ExitCode::gpu_failure, ErrorKind::gpu_failure, code, "metal",
               message};
}

} // namespace

struct Device::Impl {
  detail::MetalOwner<MTL::Device> device;
  detail::MetalOwner<MTL::CommandQueue> queue;
  std::atomic<bool> lost{false};
};

Device::Device(std::unique_ptr<Impl> impl, DeviceReport report) noexcept
    : impl_(std::move(impl)), report_(std::move(report)) {}

Device::Device(Device &&) noexcept = default;
Device &Device::operator=(Device &&) noexcept = default;

Device::~Device() = default;

const DeviceReport &Device::report() const noexcept { return report_; }

bool Device::is_lost() const noexcept {
  return impl_ != nullptr && impl_->lost.load(std::memory_order_acquire);
}

std::expected<void, Error> Device::ensure_alive() const {
  if (is_lost() || impl_ == nullptr || !impl_->device) {
    return std::unexpected(gpu_error(
        "metal_device_lost", "Metal device was removed or marked lost"));
  }
  return {};
}

void Device::inject_device_lost_for_test() noexcept {
  if (impl_ != nullptr) {
    impl_->lost.store(true, std::memory_order_release);
  }
}

std::expected<Device, Error> Device::open(CapabilityRequirements requirements,
                                          DeviceOpenFaults faults) {
  detail::MetalOwner<NS::AutoreleasePool> pool{
      NS::AutoreleasePool::alloc()->init()};
  if (!pool) {
    return std::unexpected(gpu_error("autorelease_pool_failed",
                                     "Cannot create Metal autorelease pool"));
  }

  if (faults.force_no_device) {
    return std::unexpected(gpu_error("metal_device_unavailable",
                                     "No system Metal device is available"));
  }

  detail::MetalOwner<MTL::Device> device{MTL::CreateSystemDefaultDevice()};
  if (!device) {
    return std::unexpected(gpu_error("metal_device_unavailable",
                                     "No system Metal device is available"));
  }

  const bool supports_apple7 =
      device->supportsFamily(MTL::GPUFamily::GPUFamilyApple7);
  const bool supports_metal3 =
      device->supportsFamily(MTL::GPUFamily::GPUFamilyMetal3);
  const bool has_unified = device->hasUnifiedMemory();

  if (faults.force_unsupported_family ||
      (requirements.require_apple7 && !supports_apple7)) {
    return std::unexpected(
        gpu_error("unsupported_gpu_family",
                  "Default Metal device lacks Apple7 (M1 baseline)"));
  }
  if (requirements.require_metal3 && !supports_metal3) {
    return std::unexpected(
        gpu_error("unsupported_feature",
                  "Metal3 family required but unavailable on this device"));
  }
  if (requirements.require_unified_memory && !has_unified) {
    return std::unexpected(
        gpu_error("unsupported_feature",
                  "Unified memory required but unavailable on this device"));
  }

  if (faults.force_no_queue) {
    return std::unexpected(gpu_error("metal_queue_unavailable",
                                     "Cannot create a Metal command queue"));
  }

  detail::MetalOwner<MTL::CommandQueue> queue{device->newCommandQueue()};
  if (!queue) {
    return std::unexpected(gpu_error("metal_queue_unavailable",
                                     "Cannot create a Metal command queue"));
  }
  if (requirements.require_command_queue && !queue) {
    return std::unexpected(gpu_error("unsupported_feature",
                                     "Command queue required but unavailable"));
  }

  DeviceReport report;
  report.name = copy_utf8(device->name());
  if (const MTL::Architecture *architecture = device->architecture()) {
    report.architecture = copy_utf8(architecture->name());
  }
  report.recommended_max_working_set_bytes =
      device->recommendedMaxWorkingSetSize();
  report.max_buffer_length_bytes =
      static_cast<std::uint64_t>(device->maxBufferLength());
  report.has_unified_memory = has_unified;
  report.is_low_power = device->isLowPower();
  report.is_headless = device->isHeadless();
  report.supports_apple7 = supports_apple7;
  report.supports_metal3 = supports_metal3;
  report.has_command_queue = true;

  if (faults.force_unnamed || report.name.empty()) {
    return std::unexpected(gpu_error("metal_device_unnamed",
                                     "Metal device did not report a name"));
  }

  // Device-loss is published through `lost` + ensure_alive(). OS removal
  // observers are deferred until a process-stable handler ownership path is
  // proven; injected loss covers the structured-error contract in tests.
  auto impl = std::unique_ptr<Impl>(new Impl{});
  impl->device = std::move(device);
  impl->queue = std::move(queue);

  return Device{std::move(impl), std::move(report)};
}

namespace detail {

DeviceNative native_handles(Device &device) noexcept {
  return DeviceNative{device.impl_->device.get(), device.impl_->queue.get()};
}

} // namespace detail

} // namespace instantee::render_metal
