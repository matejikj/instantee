#include <instantee/render_metal/device.hpp>

#include "metal_owner.hpp"

#include <utility>

namespace instantee::spike::metal {
namespace {

[[nodiscard]] std::string copy_utf8(const NS::String *value) {
  if (value == nullptr) {
    return {};
  }
  const char *utf8 = value->utf8String();
  return utf8 == nullptr ? std::string{} : std::string{utf8};
}

} // namespace

struct Device::Impl {
  detail::MetalOwner<MTL::Device> device;
  detail::MetalOwner<MTL::CommandQueue> queue;
};

Device::Device(std::unique_ptr<Impl> impl, DeviceReport report) noexcept
    : impl_(std::move(impl)), report_(std::move(report)) {}

Device::Device(Device &&) noexcept = default;
Device &Device::operator=(Device &&) noexcept = default;
Device::~Device() = default;

const DeviceReport &Device::report() const noexcept { return report_; }

std::expected<Device, Error> Device::open() {
  detail::MetalOwner<NS::AutoreleasePool> pool{
      NS::AutoreleasePool::alloc()->init()};
  if (!pool) {
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "autorelease_pool_failed", "metal",
                                 "Cannot create Metal autorelease pool"});
  }

  detail::MetalOwner<MTL::Device> device{MTL::CreateSystemDefaultDevice()};
  if (!device) {
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "metal_device_unavailable", "metal",
                                 "No system Metal device is available"});
  }

  const bool supports_apple7 =
      device->supportsFamily(MTL::GPUFamily::GPUFamilyApple7);
  if (!supports_apple7) {
    return std::unexpected(
        Error{ExitCode::gpu_failure, "unsupported_gpu_family", "metal",
              "Default Metal device lacks Apple7 (M1 baseline)"});
  }

  detail::MetalOwner<MTL::CommandQueue> queue{device->newCommandQueue()};
  if (!queue) {
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "metal_queue_unavailable", "metal",
                                 "Cannot create a Metal command queue"});
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
  report.has_unified_memory = device->hasUnifiedMemory();
  report.is_low_power = device->isLowPower();
  report.is_headless = device->isHeadless();
  report.supports_apple7 = supports_apple7;
  report.supports_metal3 =
      device->supportsFamily(MTL::GPUFamily::GPUFamilyMetal3);
  report.has_command_queue = true;

  if (report.name.empty()) {
    return std::unexpected(Error{ExitCode::gpu_failure, "metal_device_unnamed",
                                 "metal",
                                 "Metal device did not report a name"});
  }

  auto impl =
      std::unique_ptr<Impl>(new Impl{std::move(device), std::move(queue)});
  return Device{std::move(impl), std::move(report)};
}

} // namespace instantee::spike::metal
