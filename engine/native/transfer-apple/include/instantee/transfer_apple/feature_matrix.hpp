#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_metal/device.hpp>

#include <cstdint>
#include <expected>
#include <string>

namespace instantee::spike::transfer {
using diagnostics::Error;
using diagnostics::ExitCode;

// Feature and SDK facts for the Metal ↔ Core Video transfer experiment.
// Strings carry version text only; no host or user identity is recorded.
struct TransferFeatureMatrix {
  bool metal_device_available = false;
  bool apple7_family = false;
  bool metal3 = false;
  bool unified_memory = false;
  bool low_power = false;
  bool headless = false;
  bool cv_metal_texture_cache = false;
  bool metal_compatible_pixel_buffer = false;
  bool iosurface_backed_pixel_buffer = false;
  bool shared_surface_render = false;
  std::uint64_t recommended_max_working_set_bytes = 0;
  std::string device_name;
  std::string device_architecture;
  std::string macos_product_version;
  std::string macos_build;
  std::string selected_sdk_version;
  std::string deployment_target;
};

// Probes the live Metal device plus Core Video/IOSurface capabilities used by
// the shared-surface path. Failure is a structured error, never a silent
// fallback to an unverified feature set.
[[nodiscard]] std::expected<TransferFeatureMatrix, Error>
probe_transfer_features(render_metal::Device &device);

} // namespace instantee::spike::transfer
