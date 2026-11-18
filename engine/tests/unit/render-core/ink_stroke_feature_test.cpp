#include <instantee/render_core/ink_stroke.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__)
#error "render-core tests must remain free of Metal/Apple headers"
#endif

namespace {

using namespace instantee::render_core;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  auto registry = make_ink_stroke_shader_feature_registry();
  expect(registry.contains(kInkStrokeFeature), "feature registered");

  MaterialDesc material{.id = "ink",
                        .version = 1,
                        .feature_names = {std::string{kInkStrokeFeature}}};
  expect(registry.validate_material(material).has_value(), "material ok");

  auto active = plan_ink_stroke_passes(registry, material,
                                       {std::string{kInkStrokeFeature}});
  expect(active.has_value() && active->ink_stroke_active, "pass active");
  expect(active && !active->pass_keys.empty() &&
             active->pass_keys.front() == kInkStrokePass,
         "pass key");

  auto inactive = plan_ink_stroke_passes(registry, material, /*active*/ {});
  expect(inactive.has_value() && !inactive->ink_stroke_active &&
             inactive->pass_keys.empty(),
         "inactive empty pass");

  InkStrokeShaderParams ok{};
  expect(validate_ink_stroke_params(ok).has_value(), "default params");
  InkStrokeShaderParams bad{.progress = std::nanf("")};
  expect(!validate_ink_stroke_params(bad).has_value(), "reject nan progress");

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "ink_stroke_feature_test: ok\n";
  return EXIT_SUCCESS;
}
