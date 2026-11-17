#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/metal_backend.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  using namespace instantee::render_core;
  using namespace instantee::render_metal;

  // Structured errors: absence / unsupported feature (injected faults).
  {
    DeviceOpenFaults no_gpu{.force_no_device = true};
    auto missing = Device::open({}, no_gpu);
    expect(!missing.has_value(), "force_no_device fails");
    if (!missing) {
      expect(missing.error().code == "metal_device_unavailable",
             "absence code");
      expect(missing.error().kind == ErrorKind::gpu_failure, "absence kind");
    }

    DeviceOpenFaults bad_family{.force_unsupported_family = true};
    auto unsupported = Device::open({}, bad_family);
    expect(!unsupported.has_value(), "force_unsupported_family fails");
    if (!unsupported) {
      expect(unsupported.error().code == "unsupported_gpu_family",
             "unsupported family code");
    }

    CapabilityRequirements need_metal3{.require_apple7 = true,
                                       .require_metal3 = true};
    DeviceOpenFaults force_family{.force_unsupported_family = true};
    auto feature = Device::open(need_metal3, force_family);
    expect(!feature.has_value(), "capability negotiation rejects");
  }

  // Smoke parity: real device open + report shape matches prior spike smoke.
  auto device = Device::open();
  expect(device.has_value(), "open default device");
  if (!device) {
    return EXIT_FAILURE;
  }
  const DeviceReport report = device->report();
  expect(!report.name.empty(), "device name");
  expect(report.supports_apple7, "apple7 baseline");
  expect(report.has_command_queue, "command queue");
  expect(report.max_buffer_length_bytes > 0, "buffer length");
  expect(report.name.find("Apple") != std::string::npos, "Apple device name");

  auto owner = MetalSubmitOwner::create(std::move(*device));
  expect(owner.has_value(), "create submit owner");
  if (!owner) {
    return EXIT_FAILURE;
  }

  PassDesc pass{};
  pass.width_px = 64;
  pass.height_px = 64;
  pass.color_target = 1;
  pass.ops.push_back(DrawOp{.kind = DrawOpKind::clear});
  expect(owner->submit(pass).has_value(), "first submit");
  expect(owner->submit_count() == 1, "submit count");
  expect(owner->submit(pass).has_value(), "second sequential submit");
  expect(owner->submit_count() == 2, "submit count advances");

  // Device loss: subsequent submit must fail with structured error.
  owner->device().inject_device_lost_for_test();
  expect(owner->device().is_lost(), "lost flag");
  auto lost = owner->submit(pass);
  expect(!lost.has_value(), "submit rejects lost device");
  if (!lost) {
    expect(lost.error().code == "metal_device_lost", "device_lost code");
    expect(lost.error().kind == ErrorKind::gpu_failure, "device_lost kind");
  }
  expect(owner->submit_count() == 2, "failed submit does not count");

  // Opening after loss of another device still works (smoke parity).
  auto again = Device::open();
  expect(again.has_value(), "open after unrelated loss");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
