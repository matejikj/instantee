#pragma once

#include <instantee/render_metal/device.hpp>

#include <Metal/Metal.hpp>

namespace instantee::spike::metal::detail {

struct DeviceNative {
  MTL::Device *device = nullptr;
  MTL::CommandQueue *queue = nullptr;
};

[[nodiscard]] DeviceNative native_handles(Device &device) noexcept;

} // namespace instantee::spike::metal::detail
