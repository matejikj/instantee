#pragma once

#include <instantee/core_model/ids.hpp>
#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace instantee::core {

struct PropertyIdTag;
using PropertyId = TypedId<PropertyIdTag>;

enum class PropertyValueKind : std::uint8_t { scalar = 0, vec3, color_rgba };

// What kind of animation target may bind to a property. Styles register
// material/path channels without editing a closed style-name switch.
enum class AnimatableTargetKind : std::uint8_t { node = 0, material, path };

struct PropertyDescriptor {
  PropertyId id{};
  PropertyValueKind value_kind = PropertyValueKind::scalar;
  AnimatableTargetKind target_kind = AnimatableTargetKind::node;
};

// Map of property name → descriptor. Builtins cover transforms/opacity/color/
// path_length; additional material parameters are added via register_property.
class PropertyRegistry {
public:
  [[nodiscard]] std::expected<void, diagnostics::Error>
  register_property(PropertyDescriptor descriptor);

  [[nodiscard]] std::expected<PropertyDescriptor, diagnostics::Error>
  resolve(std::string_view property_name) const;

  // Fails when the property is unknown or not allowed for target_kind.
  [[nodiscard]] std::expected<PropertyDescriptor, diagnostics::Error>
  resolve_for_target(AnimatableTargetKind target_kind,
                     std::string_view property_name) const;

  [[nodiscard]] bool contains(std::string_view property_name) const;

  [[nodiscard]] std::vector<std::string> names() const;

private:
  std::unordered_map<std::string, PropertyDescriptor> by_name_;
};

[[nodiscard]] PropertyRegistry make_builtin_property_registry();

} // namespace instantee::core
