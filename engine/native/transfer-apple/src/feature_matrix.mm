#include <instantee/transfer_apple/feature_matrix.hpp>

#include <instantee/export_apple/pixel_buffer.hpp>

#include "cf_owner.hpp"
#include "device_native.hpp"
#include "pixel_buffer_native.hpp"

#include <CoreVideo/CoreVideo.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.hpp>

#include <sys/sysctl.h>

#include <string>

#if !__has_feature(objc_arc)
#error "The Apple transfer bridge requires ARC"
#endif

namespace instantee::spike::transfer {
namespace {

constexpr const char *kPhase = "transfer_features";

[[nodiscard]] std::string sysctl_string(const char *name) noexcept {
  char buffer[128];
  std::size_t size = sizeof(buffer);
  if (sysctlbyname(name, buffer, &size, nullptr, 0) != 0 || size == 0U) {
    return {};
  }
  if (buffer[size - 1U] == '\0') {
    return std::string{buffer};
  }
  return std::string{buffer, size};
}

} // namespace

std::expected<TransferFeatureMatrix, Error>
probe_transfer_features(render_metal::Device &device) {
  TransferFeatureMatrix matrix;
  const auto &report = device.report();
  matrix.metal_device_available = report.has_command_queue;
  matrix.apple7_family = report.supports_apple7;
  matrix.metal3 = report.supports_metal3;
  matrix.unified_memory = report.has_unified_memory;
  matrix.low_power = report.is_low_power;
  matrix.headless = report.is_headless;
  matrix.recommended_max_working_set_bytes =
      report.recommended_max_working_set_bytes;
  matrix.device_name = report.name;
  matrix.device_architecture = report.architecture;
  matrix.macos_build = sysctl_string("kern.osversion");
  matrix.deployment_target = INSTANTEE_TRANSFER_DEPLOYMENT_TARGET;
  matrix.selected_sdk_version = INSTANTEE_TRANSFER_SDK_VERSION;

  @autoreleasepool {
    const NSOperatingSystemVersion version =
        NSProcessInfo.processInfo.operatingSystemVersion;
    matrix.macos_product_version = std::to_string(version.majorVersion) + "." +
                                   std::to_string(version.minorVersion) + "." +
                                   std::to_string(version.patchVersion);
  }

  const render_metal::detail::DeviceNative native =
      render_metal::detail::native_handles(device);
  if (native.device == nullptr || !matrix.metal_device_available) {
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "metal_device_unavailable", kPhase,
                                 "Metal device handles are unavailable"});
  }

  CVMetalTextureCacheRef raw_cache = nullptr;
  const CVReturn cache_status = CVMetalTextureCacheCreate(
      kCFAllocatorDefault, nullptr, (__bridge id<MTLDevice>)native.device,
      nullptr, &raw_cache);
  detail::CfOwner<CVMetalTextureCacheRef> cache = detail::adopt_cf(raw_cache);
  matrix.cv_metal_texture_cache =
      cache_status == kCVReturnSuccess && static_cast<bool>(cache);

  auto pool = apple::PixelBufferPool::create(
      apple::PixelBufferFormat::media_1080x1920(), 1U,
      apple::PixelBufferCompatibility::metal_iosurface);
  if (!pool) {
    return matrix;
  }
  matrix.metal_compatible_pixel_buffer = true;

  auto buffer = pool->acquire();
  if (!buffer) {
    return matrix;
  }
  CVPixelBufferRef cv_buffer = apple::detail::borrow_cv_pixel_buffer(*buffer);
  if (cv_buffer == nullptr) {
    return matrix;
  }
  matrix.iosurface_backed_pixel_buffer =
      CVPixelBufferGetIOSurface(cv_buffer) != nullptr;

  if (matrix.cv_metal_texture_cache && matrix.iosurface_backed_pixel_buffer) {
    CVMetalTextureRef raw_texture = nullptr;
    const CVReturn texture_status = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, cache.get(), cv_buffer, nullptr,
        MTLPixelFormatBGRA8Unorm, CVPixelBufferGetWidth(cv_buffer),
        CVPixelBufferGetHeight(cv_buffer), 0, &raw_texture);
    detail::CfOwner<CVMetalTextureRef> texture = detail::adopt_cf(raw_texture);
    const id<MTLTexture> mtl_texture =
        texture ? CVMetalTextureGetTexture(texture.get()) : nil;
    matrix.shared_surface_render =
        texture_status == kCVReturnSuccess && mtl_texture != nil;
  }

  return matrix;
}

} // namespace instantee::spike::transfer
