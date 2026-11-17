#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

namespace instantee::render_metal {
using diagnostics::Error;
using diagnostics::ErrorKind;
using diagnostics::ExitCode;

class Device;

namespace detail {
struct DeviceNative;
[[nodiscard]] DeviceNative native_handles(Device &device) noexcept;
} // namespace detail

// Value report with explicit byte units. Contains no Metal or Objective-C
// types in the public surface.
struct DeviceReport {
  std::string name;
  std::string architecture;
  std::uint64_t recommended_max_working_set_bytes = 0;
  std::uint64_t max_buffer_length_bytes = 0;
  bool has_unified_memory = false;
  bool is_low_power = false;
  bool is_headless = false;
  bool supports_apple7 = false;
  bool supports_metal3 = false;
  bool has_command_queue = false;
};

// Explicit M1-oriented capability negotiation. Unsupported requirements become
// structured gpu_failure errors instead of silent feature probing later.
struct CapabilityRequirements {
  bool require_apple7 = true;
  bool require_metal3 = false;
  bool require_unified_memory = false;
  bool require_command_queue = true;
};

// Test-only fault injection. Production callers leave every flag false.
struct DeviceOpenFaults {
  bool force_no_device = false;
  bool force_unsupported_family = false;
  bool force_no_queue = false;
  bool force_unnamed = false;
};

// Owns one system Metal device and one command queue. Public surface stays
// C++-only; Metal types remain behind the implementation. Autorelease pools
// wrap open/submit boundaries inside the .cpp TU.
class Device {
public:
  Device(Device &&) noexcept;
  Device &operator=(Device &&) noexcept;
  ~Device();

  Device(const Device &) = delete;
  Device &operator=(const Device &) = delete;

  // Creates a headless-capable default device matching `requirements` and a
  // command queue. Missing Metal, unmet capabilities, or injected faults return
  // structured GPU errors; retained objects are released on every failure path.
  [[nodiscard]] static std::expected<Device, Error>
  open(CapabilityRequirements requirements = {}, DeviceOpenFaults faults = {});

  [[nodiscard]] const DeviceReport &report() const noexcept;

  // True after explicit loss publication (test injection today). Submit paths
  // must fail with metal_device_lost once set; the device is not revived.
  [[nodiscard]] bool is_lost() const noexcept;

  // Returns success while the device remains usable; otherwise
  // metal_device_lost.
  [[nodiscard]] std::expected<void, Error> ensure_alive() const;

  // Test hook: marks the device lost without waiting for a real GPU removal.
  void inject_device_lost_for_test() noexcept;

private:
  friend detail::DeviceNative detail::native_handles(Device &device) noexcept;

  struct Impl;
  Device(std::unique_ptr<Impl> impl, DeviceReport report) noexcept;

  std::unique_ptr<Impl> impl_;
  DeviceReport report_;
};

} // namespace instantee::render_metal
