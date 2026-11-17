#include <instantee/core_model/properties.hpp>

#include <utility>

namespace instantee::core {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "properties", message};
}

std::expected<void, diagnostics::Error>
add_builtin(PropertyRegistry &registry, std::string_view name,
            PropertyValueKind value_kind, AnimatableTargetKind target_kind) {
  auto id = PropertyId::make(name);
  if (!id) {
    return std::unexpected(id.error());
  }
  PropertyDescriptor descriptor;
  descriptor.id = *id;
  descriptor.value_kind = value_kind;
  descriptor.target_kind = target_kind;
  return registry.register_property(std::move(descriptor));
}

} // namespace

std::expected<void, diagnostics::Error>
PropertyRegistry::register_property(PropertyDescriptor descriptor) {
  const std::string name(descriptor.id.value());
  if (name.empty()) {
    return std::unexpected(err("empty_id", "Property id is empty"));
  }
  if (!by_name_.emplace(name, std::move(descriptor)).second) {
    return std::unexpected(
        err("duplicate_property", "Property is already registered"));
  }
  return {};
}

std::expected<PropertyDescriptor, diagnostics::Error>
PropertyRegistry::resolve(std::string_view property_name) const {
  const auto it = by_name_.find(std::string(property_name));
  if (it == by_name_.end()) {
    return std::unexpected(
        err("unknown_property", "Property is not registered"));
  }
  return it->second;
}

std::expected<PropertyDescriptor, diagnostics::Error>
PropertyRegistry::resolve_for_target(AnimatableTargetKind target_kind,
                                     std::string_view property_name) const {
  auto descriptor = resolve(property_name);
  if (!descriptor) {
    return std::unexpected(descriptor.error());
  }
  if (descriptor->target_kind != target_kind) {
    return std::unexpected(
        err("unsupported_target_property",
            "Property is not valid for the requested animation target kind"));
  }
  return descriptor;
}

bool PropertyRegistry::contains(std::string_view property_name) const {
  return by_name_.contains(std::string(property_name));
}

std::vector<std::string> PropertyRegistry::names() const {
  std::vector<std::string> out;
  out.reserve(by_name_.size());
  for (const auto &entry : by_name_) {
    out.push_back(entry.first);
  }
  return out;
}

PropertyRegistry make_builtin_property_registry() {
  PropertyRegistry registry;
  const struct {
    std::string_view name;
    PropertyValueKind value_kind;
    AnimatableTargetKind target_kind;
  } builtins[] = {
      {"translate_x", PropertyValueKind::scalar, AnimatableTargetKind::node},
      {"translate_y", PropertyValueKind::scalar, AnimatableTargetKind::node},
      {"translate_z", PropertyValueKind::scalar, AnimatableTargetKind::node},
      {"scale_x", PropertyValueKind::scalar, AnimatableTargetKind::node},
      {"scale_y", PropertyValueKind::scalar, AnimatableTargetKind::node},
      {"scale_z", PropertyValueKind::scalar, AnimatableTargetKind::node},
      {"rotation_z_rad", PropertyValueKind::scalar, AnimatableTargetKind::node},
      {"opacity", PropertyValueKind::scalar, AnimatableTargetKind::node},
      {"color", PropertyValueKind::color_rgba, AnimatableTargetKind::material},
      {"path_length", PropertyValueKind::scalar, AnimatableTargetKind::path},
  };
  for (const auto &builtin : builtins) {
    (void)add_builtin(registry, builtin.name, builtin.value_kind,
                      builtin.target_kind);
  }
  return registry;
}

} // namespace instantee::core
