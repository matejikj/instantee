#include <instantee/render_core/shader_features.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__) ||  \
    defined(COREVIDEO_H)
#error "render-core tests must remain free of Metal/Apple headers"
#endif

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

  auto registry = make_builtin_shader_feature_registry();
  expect(registry.contains("test_pattern"), "builtin test_pattern");
  expect(registry.contains("noop_effect"), "builtin noop");

  auto unknown = registry.resolve("not_a_feature");
  expect(!unknown.has_value(), "unknown feature rejected");
  if (!unknown) {
    expect(unknown.error().code == "unknown_shader_feature", "unknown code");
  }

  MaterialDesc material{.id = "demo",
                        .version = 1,
                        .feature_names = {"test_pattern", "noop_effect"}};
  expect(registry.validate_material(material).has_value(), "material ok");

  MaterialDesc bad{.id = "bad", .version = 1, .feature_names = {"missing"}};
  expect(!registry.validate_material(bad).has_value(), "bad material");
  if (auto err = registry.validate_material(bad); !err) {
    expect(err.error().code == "unknown_shader_feature", "bad material code");
  }

  // Active test_pattern contributes main_color; noop contributes nothing.
  auto plan = registry.plan_passes(
      material, std::vector<std::string>{"test_pattern", "noop_effect"});
  expect(plan.has_value(), "plan with both active");
  if (plan) {
    expect(plan->pass_keys.size() == 1, "single pass from test_pattern");
    expect(plan->pass_keys[0] == "main_color", "main_color key");
  }

  // Empty-pass invariant: only noop active → empty plan.
  auto empty_plan =
      registry.plan_passes(material, std::vector<std::string>{"noop_effect"});
  expect(empty_plan.has_value(), "empty plan compiles");
  if (empty_plan) {
    expect(empty_plan->pass_keys.empty(), "empty-pass invariant");
  }

  // Inactive features add nothing even when listed on the material.
  auto inactive = registry.plan_passes(material, std::vector<std::string>{});
  expect(inactive.has_value() && inactive->pass_keys.empty(),
         "all inactive => no passes");

  // Unknown active name fails planning.
  auto boom = registry.plan_passes(
      material, std::vector<std::string>{"test_pattern", "ghost"});
  expect(!boom.has_value(), "unknown active rejected");
  if (!boom) {
    expect(boom.error().code == "unknown_shader_feature",
           "active unknown code");
  }

  // Version mismatch.
  auto versioned = registry.resolve("test_pattern", 99);
  expect(!versioned.has_value(), "version mismatch");
  if (!versioned) {
    expect(versioned.error().code == "shader_feature_version_mismatch",
           "version code");
  }

  // No style-name API: registry keys are feature ids only.
  expect(!registry.contains("paper-cinema"), "no style-name branching key");
  expect(!registry.contains("editorial"), "no editorial style key");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
