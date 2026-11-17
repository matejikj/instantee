#include <instantee/render_metal/device.hpp>

#include <string>
#include <type_traits>
#include <utility>

int main() {
  using instantee::render_metal::Device;
  using instantee::render_metal::DeviceReport;
  static_assert(!std::is_copy_constructible_v<Device>);
  static_assert(!std::is_copy_assignable_v<Device>);
  static_assert(std::is_nothrow_move_constructible_v<Device>);
  static_assert(std::is_nothrow_move_assignable_v<Device>);
  static_assert(std::is_nothrow_destructible_v<Device>);

  auto first = Device::open();
  if (!first) {
    return 1;
  }
  const DeviceReport original = first->report();
  if (original.name.empty() || !original.supports_apple7 ||
      !original.has_command_queue || original.max_buffer_length_bytes == 0 ||
      original.recommended_max_working_set_bytes == 0) {
    return 2;
  }

  Device moved{std::move(*first)};
  if (moved.report().name != original.name ||
      moved.report().supports_apple7 != original.supports_apple7 ||
      moved.report().has_command_queue != original.has_command_queue) {
    return 3;
  }

  // Opening again must succeed while the moved device still owns its queue.
  auto second = Device::open();
  if (!second) {
    return 4;
  }
  if (second->report().name != original.name ||
      !second->report().supports_apple7) {
    return 5;
  }

  // Drop ownership in a nested scope; destruction must not disturb the other.
  {
    Device retiring{std::move(moved)};
    if (retiring.report().name != original.name) {
      return 6;
    }
  }
  if (second->report().architecture != original.architecture) {
    return 7;
  }
  // Keep a live reference to the moved-from name storage until after
  // retirement.
  if (original.name.find("Apple") == std::string::npos) {
    return 8;
  }
}
