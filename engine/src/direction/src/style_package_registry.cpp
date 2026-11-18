#include <instantee/direction/style_package_registry.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "style_package_registry", message};
}

diagnostics::Error make_incompatible(std::string_view code,
                                     std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::incompatible_schema,
                            diagnostics::ErrorKind::incompatible_schema, code,
                            "style_package_registry", message};
}

[[nodiscard]] bool valid_id(std::string_view text) noexcept {
  return !text.empty() && text.size() <= 64U;
}

[[nodiscard]] std::expected<std::uint32_t, diagnostics::Error>
parse_component(std::string_view text) {
  if (text.empty() || text.size() > 10U) {
    return std::unexpected(make_invalid(
        "invalid_semver", "Semver component is empty or too long"));
  }
  if (text.size() > 1U && text.front() == '0') {
    return std::unexpected(
        make_invalid("invalid_semver", "Semver component has a leading zero"));
  }
  std::uint32_t value = 0;
  for (char ch : text) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return std::unexpected(
          make_invalid("invalid_semver", "Semver component is not decimal"));
    }
    const std::uint32_t digit = static_cast<std::uint32_t>(ch - '0');
    if (value > (UINT32_MAX - digit) / 10U) {
      return std::unexpected(
          make_invalid("invalid_semver", "Semver component overflows"));
    }
    value = value * 10U + digit;
  }
  return value;
}

[[nodiscard]] bool capability_provided(const EngineCapabilities &engine,
                                       std::string_view need) {
  return std::find(engine.provided.begin(), engine.provided.end(), need) !=
         engine.provided.end();
}

} // namespace

std::expected<PackageSemVer, diagnostics::Error>
parse_package_semver(std::string_view text) {
  const auto first = text.find('.');
  const auto second = first == std::string_view::npos
                          ? std::string_view::npos
                          : text.find('.', first + 1);
  if (first == std::string_view::npos || second == std::string_view::npos ||
      text.find('.', second + 1) != std::string_view::npos) {
    return std::unexpected(
        make_invalid("invalid_semver", "Version must be MAJOR.MINOR.PATCH"));
  }
  auto major = parse_component(text.substr(0, first));
  if (!major) {
    return std::unexpected(major.error());
  }
  auto minor = parse_component(text.substr(first + 1, second - first - 1));
  if (!minor) {
    return std::unexpected(minor.error());
  }
  auto patch = parse_component(text.substr(second + 1));
  if (!patch) {
    return std::unexpected(patch.error());
  }
  return PackageSemVer{*major, *minor, *patch};
}

std::expected<void, diagnostics::Error>
StylePackageRegistry::register_package(StylePackageDesc descriptor) {
  if (!valid_id(descriptor.package_id)) {
    return std::unexpected(
        make_invalid("invalid_package_id", "Style package id is empty"));
  }
  auto version = parse_package_semver(descriptor.version);
  if (!version) {
    return std::unexpected(version.error());
  }
  auto minimum = parse_package_semver(descriptor.minimum_engine_version);
  if (!minimum) {
    return std::unexpected(minimum.error());
  }
  for (const std::string &capability : descriptor.required_capabilities) {
    if (capability.empty()) {
      return std::unexpected(make_invalid(
          "invalid_capability", "Required capability id must be non-empty"));
    }
  }
  for (const std::string &component : descriptor.registered_components) {
    if (component.empty()) {
      return std::unexpected(make_invalid(
          "invalid_component", "Registered component id must be non-empty"));
    }
  }
  for (const std::string &feature : descriptor.shader_features) {
    if (feature.empty()) {
      return std::unexpected(make_invalid(
          "invalid_shader_feature", "Shader feature id must be non-empty"));
    }
  }

  auto &versions = by_id_[descriptor.package_id];
  const auto [it, inserted] =
      versions.emplace(descriptor.version, std::move(descriptor));
  if (!inserted) {
    return std::unexpected(
        make_invalid("duplicate_style_package",
                     "Style package revision is already registered"));
  }
  (void)it;
  (void)version;
  (void)minimum;
  return {};
}

bool StylePackageRegistry::contains(std::string_view package_id) const {
  return by_id_.contains(std::string{package_id});
}

bool StylePackageRegistry::contains(std::string_view package_id,
                                    std::string_view version) const {
  const auto it = by_id_.find(std::string{package_id});
  if (it == by_id_.end()) {
    return false;
  }
  return it->second.contains(std::string{version});
}

std::expected<StylePackageDesc, diagnostics::Error>
StylePackageRegistry::resolve(std::string_view package_id,
                              std::string_view version) const {
  const auto package = by_id_.find(std::string{package_id});
  if (package == by_id_.end()) {
    return std::unexpected(make_invalid("unknown_style_package",
                                        "Style package id is not registered"));
  }
  const auto revision = package->second.find(std::string{version});
  if (revision == package->second.end()) {
    return std::unexpected(
        make_incompatible("outdated_style_package",
                          "Requested style package version is not registered"));
  }
  return revision->second;
}

std::expected<StylePackageDesc, diagnostics::Error>
StylePackageRegistry::check_before_render(
    std::string_view package_id, std::string_view version,
    const EngineCapabilities &engine) const {
  auto package = resolve(package_id, version);
  if (!package) {
    return package;
  }

  auto minimum = parse_package_semver(package->minimum_engine_version);
  if (!minimum) {
    return std::unexpected(minimum.error());
  }
  if (engine.engine_version < *minimum) {
    return std::unexpected(
        make_incompatible("incompatible_style_package",
                          "Engine version is below the style package minimum"));
  }

  for (const std::string &need : package->required_capabilities) {
    if (!capability_provided(engine, need)) {
      return std::unexpected(make_incompatible(
          "missing_style_capability",
          "Host engine lacks a capability required by the style package"));
    }
  }
  return package;
}

std::vector<std::string> StylePackageRegistry::package_ids() const {
  std::vector<std::string> ids;
  ids.reserve(by_id_.size());
  for (const auto &entry : by_id_) {
    ids.push_back(entry.first);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

std::vector<std::string>
StylePackageRegistry::versions_for(std::string_view package_id) const {
  const auto it = by_id_.find(std::string{package_id});
  if (it == by_id_.end()) {
    return {};
  }
  std::vector<std::string> versions;
  versions.reserve(it->second.size());
  for (const auto &entry : it->second) {
    versions.push_back(entry.first);
  }
  std::sort(versions.begin(), versions.end());
  return versions;
}

EngineCapabilities
make_default_engine_capabilities(PackageSemVer engine_version) {
  return EngineCapabilities{
      .engine_version = engine_version,
      .provided =
          {
              "path_geometry",
              "text",
              "camera",
              "tracks",
              "export",
              "typography",
              "time",
          },
  };
}

StylePackageRegistry make_builtin_style_package_registry() {
  StylePackageRegistry registry;

  StylePackageDesc editorial{
      .package_id = "editorial",
      .version = "1.0.0",
      .minimum_engine_version = "0.1.0",
      .required_capabilities = {"path_geometry", "text", "camera", "tracks",
                                "export", "typography", "time"},
      .registered_components = {"kinetic_headline", "halftone_panel",
                                "neo_brutal_frame"},
      .shader_features = {"solid-fill", "halftone"},
      .material_presets = {"flat_ink", "halftone_panel"},
  };
  StylePackageDesc paper{
      .package_id = "paper-cinema",
      .version = "1.0.0",
      .minimum_engine_version = "0.1.0",
      .required_capabilities = {"path_geometry", "text", "camera", "tracks",
                                "export", "typography", "time"},
      .registered_components = {"puppet_rig", "depth_stage", "paper_fold",
                                "epoch_variant"},
      .shader_features = {"paper-sheet", "paper-fold", "solid-fill"},
      .material_presets = {"paper_sheet", "paper_fold", "cutout",
                           "contact_shadow"},
  };
  StylePackageDesc notebook{
      .package_id = "scientific-notebook",
      .version = "1.0.0",
      .minimum_engine_version = "0.1.0",
      .required_capabilities = {"path_geometry", "text", "camera", "tracks",
                                "export", "typography", "time"},
      .registered_components = {"ink_stroke", "math_layout", "diagram_note",
                                "sticky_note", "highlighter"},
      .shader_features = {"ink-stroke", "grid-paper", "solid-fill"},
      .material_presets = {"pen_ink", "notebook_grid", "highlighter",
                           "sticky_note"},
  };

  (void)registry.register_package(std::move(editorial));
  (void)registry.register_package(std::move(paper));
  (void)registry.register_package(std::move(notebook));
  return registry;
}

} // namespace instantee::direction
