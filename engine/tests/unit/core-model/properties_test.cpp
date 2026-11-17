#include <instantee/core_model/properties.hpp>

#include <cstdlib>
#include <iostream>

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
  using namespace instantee::core;

  auto registry = make_builtin_property_registry();
  expect(registry.contains("translate_x"), "builtin translate_x");
  expect(registry.contains("opacity"), "builtin opacity");
  expect(registry.contains("color"), "builtin color");
  expect(registry.contains("path_length"), "builtin path_length");

  {
    auto tx =
        registry.resolve_for_target(AnimatableTargetKind::node, "translate_x");
    expect(tx.has_value(), "resolve translate_x for node");
    if (tx) {
      expect(tx->value_kind == PropertyValueKind::scalar, "translate scalar");
      expect(tx->target_kind == AnimatableTargetKind::node, "translate node");
    }
  }

  {
    auto bad = registry.resolve_for_target(AnimatableTargetKind::node, "color");
    expect(!bad.has_value(), "color not valid on node");
    if (!bad) {
      expect(bad.error().code == "unsupported_target_property",
             "unsupported_target_property code");
    }
  }

  {
    auto unknown = registry.resolve("not_a_property");
    expect(!unknown.has_value(), "unknown property");
    if (!unknown) {
      expect(unknown.error().code == "unknown_property",
             "unknown_property code");
    }
  }

  // Extensibility: style packs register material params without editing
  // builtins.
  {
    auto id = PropertyId::make("material.roughness");
    expect(id.has_value(), "custom property id");
    if (id) {
      PropertyDescriptor custom;
      custom.id = *id;
      custom.value_kind = PropertyValueKind::scalar;
      custom.target_kind = AnimatableTargetKind::material;
      auto added = registry.register_property(custom);
      expect(added.has_value(), "register custom material property");
      auto resolved = registry.resolve_for_target(
          AnimatableTargetKind::material, "material.roughness");
      expect(resolved.has_value(), "resolve custom material property");
      auto dup_id = PropertyId::make("material.roughness");
      expect(dup_id.has_value(), "duplicate property id");
      if (dup_id) {
        PropertyDescriptor duplicate;
        duplicate.id = *dup_id;
        duplicate.value_kind = PropertyValueKind::scalar;
        duplicate.target_kind = AnimatableTargetKind::material;
        auto again = registry.register_property(std::move(duplicate));
        expect(!again.has_value(), "reject duplicate registration");
      }
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
