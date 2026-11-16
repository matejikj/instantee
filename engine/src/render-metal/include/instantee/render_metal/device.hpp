#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

namespace instantee::spike::metal {
using diagnostics::Error;
using diagnostics::ExitCode;

class Device;

namespace detail {
struct DeviceNative;
[[nodiscard]] DeviceNative native_handles(Device &device) noexcept;
} // namespace detail

// Value report with explicit byte units. Contains no Metal or Objective-C
// types.
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

// Owns one system Metal device and one command queue for the spike lifetime.
// Public surface stays C++-only; Metal types remain behind the implementation.
class Device {
public:
  Device(Device &&) noexcept;
  Device &operator=(Device &&) noexcept;
  ~Device();

  Device(const Device &) = delete;
  Device &operator=(const Device &) = delete;

  // Creates a headless-capable default device that supports Apple7 (M1
  // baseline) and a command queue. Missing Metal or unsupported features return
  // a structured GPU error; retained objects are released on every failure
  // path.
  [[nodiscard]] static std::expected<Device, Error> open();

  [[nodiscard]] const DeviceReport &report() const noexcept;

private:
  friend detail::DeviceNative detail::native_handles(Device &device) noexcept;

  struct Impl;
  Device(std::unique_ptr<Impl> impl, DeviceReport report) noexcept;

  std::unique_ptr<Impl> impl_;
  DeviceReport report_;
};

} // namespace instantee::spike::metal
