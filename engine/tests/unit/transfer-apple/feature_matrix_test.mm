#include <instantee/render_metal/device.hpp>
#include <instantee/transfer_apple/feature_matrix.hpp>

#if !__has_feature(objc_arc)
#error "Apple transfer tests require ARC"
#endif

namespace {

using instantee::render_metal::Device;
using instantee::spike::transfer::probe_transfer_features;

} // namespace

int main() {
  @autoreleasepool {
    auto device = Device::open();
    if (!device) {
      return 1;
    }
    auto matrix = probe_transfer_features(*device);
    if (!matrix) {
      return 2;
    }
    if (!matrix->metal_device_available || !matrix->apple7_family ||
        !matrix->unified_memory || !matrix->cv_metal_texture_cache ||
        !matrix->metal_compatible_pixel_buffer ||
        !matrix->iosurface_backed_pixel_buffer ||
        !matrix->shared_surface_render) {
      return 3;
    }
    if (matrix->device_name.empty() || matrix->device_architecture.empty() ||
        matrix->macos_product_version.empty() ||
        matrix->deployment_target.empty() ||
        matrix->selected_sdk_version.empty()) {
      return 4;
    }
    if (matrix->recommended_max_working_set_bytes == 0U) {
      return 5;
    }
    return 0;
  }
}
