#include <instantee/render_metal/shader_module.hpp>

#include "device_native.hpp"
#include "halftone_metallib.hpp"
#include "metal_owner.hpp"
#include "shared_surface_pattern_metallib.hpp"
#include "test_pattern_metallib.hpp"

#include <Metal/Metal.hpp>

#include <string_view>

namespace instantee::render_metal {
namespace {

[[nodiscard]] diagnostics::Error gpu_error(std::string_view code,
                                           std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::gpu_failure,
                            diagnostics::ErrorKind::gpu_failure, code, "metal",
                            message};
}

} // namespace

std::expected<ShaderModuleValidation, diagnostics::Error>
validate_shader_module(Device &device, const ShaderModuleDesc &module) {
  if (module.id.empty() || module.version == 0) {
    return std::unexpected(gpu_error("invalid_shader_module",
                                     "shader module id/version is invalid"));
  }
  if (module.metallib_bytes.empty()) {
    return std::unexpected(
        gpu_error("empty_metallib", "shader module metallib bytes are empty"));
  }
  if (module.entry_points.empty()) {
    return std::unexpected(gpu_error("missing_entry_points",
                                     "shader module declares no entry points"));
  }

  auto alive = device.ensure_alive();
  if (!alive) {
    return std::unexpected(alive.error());
  }

  detail::MetalOwner<NS::AutoreleasePool> pool{
      NS::AutoreleasePool::alloc()->init()};
  if (!pool) {
    return std::unexpected(gpu_error("autorelease_pool_failed",
                                     "Cannot create Metal autorelease pool"));
  }

  detail::DeviceNative native = detail::native_handles(device);
  if (native.device == nullptr) {
    return std::unexpected(gpu_error("metal_device_unavailable",
                                     "Metal device handle is unavailable"));
  }

  // DEFAULT destructor copies the buffer; queue is unused for that destructor.
  dispatch_data_t data = dispatch_data_create(
      module.metallib_bytes.data(), module.metallib_bytes.size(), nullptr,
      DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  if (data == nullptr) {
    return std::unexpected(
        gpu_error("metallib_dispatch_failed", "Cannot wrap metallib bytes"));
  }

  NS::Error *error = nullptr;
  detail::MetalOwner<MTL::Library> library{
      native.device->newLibrary(data, &error)};
  dispatch_release(data);
  if (!library) {
    return std::unexpected(gpu_error("metallib_load_failed",
                                     "Cannot load offline metallib module"));
  }

  ShaderModuleValidation report;
  report.id = module.id;
  report.version = module.version;
  report.metallib_bytes = module.metallib_bytes.size();
  report.resolved_entry_points.reserve(module.entry_points.size());
  for (const std::string &entry : module.entry_points) {
    detail::MetalOwner<MTL::Function> function{
        library->newFunction(NS::String::string(
            entry.c_str(), NS::StringEncoding::UTF8StringEncoding))};
    if (!function) {
      return std::unexpected(
          gpu_error("shader_entry_missing",
                    "Offline metallib is missing a declared entry point"));
    }
    report.resolved_entry_points.push_back(entry);
  }
  return report;
}

ShaderModuleDesc test_pattern_shader_module() {
  return ShaderModuleDesc{
      .id = "test_pattern",
      .version = 1,
      .feature_id = "test_pattern",
      .entry_points = {"vs_main", "fs_main"},
      .metallib_bytes =
          std::span<const std::uint8_t>{detail::kTestPatternMetallib,
                                        detail::kTestPatternMetallib_size},
  };
}

ShaderModuleDesc shared_surface_shader_module() {
  return ShaderModuleDesc{
      .id = "shared_surface_pattern",
      .version = 1,
      .feature_id = "test_pattern",
      .entry_points = {"vs_shared_main", "fs_shared_main"},
      .metallib_bytes =
          std::span<const std::uint8_t>{
              detail::kSharedSurfacePatternMetallib,
              detail::kSharedSurfacePatternMetallib_size},
  };
}

ShaderModuleDesc halftone_shader_module() {
  return ShaderModuleDesc{
      .id = "halftone",
      .version = 1,
      .feature_id = "halftone",
      .entry_points = {"vs_halftone_main", "fs_halftone_main"},
      .metallib_bytes =
          std::span<const std::uint8_t>{detail::kHalftoneMetallib,
                                        detail::kHalftoneMetallib_size},
  };
}

std::vector<ShaderModuleDesc> builtin_shader_modules() {
  return {test_pattern_shader_module(), shared_surface_shader_module(),
          halftone_shader_module()};
}

} // namespace instantee::render_metal
