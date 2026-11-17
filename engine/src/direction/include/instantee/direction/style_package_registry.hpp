#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace instantee::direction {

// Semver used for style package revisions and engine version gates. Kept in
// direction so the registry does not depend on scene-ir migration helpers.
struct PackageSemVer {
  std::uint32_t major = 0;
  std::uint32_t minor = 0;
  std::uint32_t patch = 0;

  [[nodiscard]] friend constexpr auto
  operator<=>(PackageSemVer, PackageSemVer) noexcept = default;
};

[[nodiscard]] std::expected<PackageSemVer, diagnostics::Error>
parse_package_semver(std::string_view text);

// Host engine surface that style packages may require. Shared modules live
// here as capability tokens; style packages never own geometry/time/type.
struct EngineCapabilities {
  PackageSemVer engine_version{};
  std::vector<std::string> provided{};
};

// Static Style Package registration record. Style-specific data and composed
// component ids live here; render-core stays free of style-name branches.
struct StylePackageDesc {
  std::string package_id;
  std::string version;
  std::string minimum_engine_version;
  std::vector<std::string> required_capabilities{};
  std::vector<std::string> registered_components{};
  std::vector<std::string> shader_features{};
  std::vector<std::string> material_presets{};
};

class StylePackageRegistry {
public:
  [[nodiscard]] std::expected<void, diagnostics::Error>
  register_package(StylePackageDesc descriptor);

  [[nodiscard]] bool contains(std::string_view package_id) const;
  [[nodiscard]] bool contains(std::string_view package_id,
                              std::string_view version) const;

  [[nodiscard]] std::expected<StylePackageDesc, diagnostics::Error>
  resolve(std::string_view package_id, std::string_view version) const;

  // Pre-render gate: unknown package, missing revision, unsatisfied engine
  // version, or missing host capability fail closed with structured
  // diagnostics.
  [[nodiscard]] std::expected<StylePackageDesc, diagnostics::Error>
  check_before_render(std::string_view package_id, std::string_view version,
                      const EngineCapabilities &engine) const;

  [[nodiscard]] std::vector<std::string> package_ids() const;
  [[nodiscard]] std::vector<std::string>
  versions_for(std::string_view package_id) const;

private:
  // package_id -> (version -> descriptor)
  std::unordered_map<std::string,
                     std::unordered_map<std::string, StylePackageDesc>>
      by_id_;
};

// Shared host capabilities for the V1 engine surface (path/text/camera/…).
[[nodiscard]] EngineCapabilities make_default_engine_capabilities(
    PackageSemVer engine_version = PackageSemVer{0, 1, 0});

// Built-in packages: editorial, paper-cinema, scientific-notebook.
[[nodiscard]] StylePackageRegistry make_builtin_style_package_registry();

} // namespace instantee::direction
