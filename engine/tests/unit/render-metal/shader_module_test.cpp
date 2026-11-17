#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/shader_module.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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
  using namespace instantee::render_metal;

  auto device = Device::open();
  expect(device.has_value(), "open Metal device");
  if (!device) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  const auto modules = builtin_shader_modules();
  expect(modules.size() == 4, "four builtin modules");

  bool saw_halftone = false;
  bool saw_print_grain = false;
  for (const ShaderModuleDesc &module : modules) {
    expect(!module.metallib_bytes.empty(), "module has metallib bytes");
    expect(!module.entry_points.empty(), "module declares entry points");
    if (module.id == "halftone") {
      saw_halftone = true;
      expect(module.feature_id == "halftone", "halftone feature id");
      expect(module.entry_points.size() == 2, "halftone entries");
    } else if (module.id == "print_grain") {
      saw_print_grain = true;
      expect(module.feature_id == "print_grain", "print_grain feature id");
      expect(module.entry_points.size() == 2, "print_grain entries");
    } else {
      expect(module.feature_id == "test_pattern", "pattern feature id");
    }

    auto report = validate_shader_module(*device, module);
    expect(report.has_value(), "metallib validates");
    if (!report) {
      std::cerr << "  module=" << module.id << " code=" << report.error().code
                << " msg=" << report.error().message << '\n';
      continue;
    }
    expect(report->id == module.id, "report id");
    expect(report->version == module.version, "report version");
    expect(report->metallib_bytes == module.metallib_bytes.size(),
           "report byte count");
    expect(report->resolved_entry_points == module.entry_points,
           "entry points resolved");
  }
  expect(saw_halftone, "halftone module present");
  expect(saw_print_grain, "print_grain module present");

  // Corrupt bytes must fail closed without crashing the process.
  ShaderModuleDesc bogus = test_pattern_shader_module();
  std::vector<std::uint8_t> trash(64, 0x3c);
  bogus.metallib_bytes = trash;
  auto bad = validate_shader_module(*device, bogus);
  expect(!bad.has_value(), "corrupt metallib rejected");
  if (!bad) {
    expect(bad.error().code == "metallib_load_failed" ||
               bad.error().code == "shader_entry_missing",
           "corrupt metallib error code");
  }

  ShaderModuleDesc missing_entry = test_pattern_shader_module();
  missing_entry.entry_points = {"vs_main", "no_such_entry"};
  auto missing = validate_shader_module(*device, missing_entry);
  expect(!missing.has_value(), "missing entry rejected");
  if (!missing) {
    expect(missing.error().code == "shader_entry_missing",
           "missing entry code");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
