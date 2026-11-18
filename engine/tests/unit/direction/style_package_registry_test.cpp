#include <instantee/direction/style_package_registry.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using instantee::direction::EngineCapabilities;
using instantee::direction::make_builtin_style_package_registry;
using instantee::direction::make_default_engine_capabilities;
using instantee::direction::PackageSemVer;
using instantee::direction::StylePackageDesc;
using instantee::direction::StylePackageRegistry;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  auto registry = make_builtin_style_package_registry();
  const auto engine = make_default_engine_capabilities();

  expect(registry.contains("editorial", "1.0.0"), "builtin editorial");
  expect(registry.contains("paper-cinema", "1.0.0"), "builtin paper");
  expect(registry.contains("scientific-notebook", "1.0.0"), "builtin notebook");
  expect(registry.package_ids().size() == 3, "three builtins");

  auto ok = registry.check_before_render("editorial", "1.0.0", engine);
  expect(ok.has_value(), "editorial ready before render");
  if (ok) {
    expect(ok->shader_features.size() >= 1, "editorial declares features");
  }

  auto paper_ok = registry.check_before_render("paper-cinema", "1.0.0", engine);
  expect(paper_ok.has_value(), "paper ready before render");
  if (paper_ok) {
    expect(std::find(paper_ok->shader_features.begin(),
                     paper_ok->shader_features.end(),
                     "paper-sheet") != paper_ok->shader_features.end(),
           "paper declares paper-sheet");
    expect(std::find(paper_ok->material_presets.begin(),
                     paper_ok->material_presets.end(),
                     "contact_shadow") != paper_ok->material_presets.end(),
           "paper declares contact_shadow");
  }

  auto notebook_ok =
      registry.check_before_render("scientific-notebook", "1.0.0", engine);
  expect(notebook_ok.has_value(), "notebook ready before render");
  if (notebook_ok) {
    expect(notebook_ok->material_presets.size() >= 4,
           "notebook materials include sticky/highlighter");
    expect(std::find(notebook_ok->registered_components.begin(),
                     notebook_ok->registered_components.end(),
                     "sticky_note") != notebook_ok->registered_components.end(),
           "notebook registers sticky_note");
  }

  auto unknown =
      registry.check_before_render("does-not-exist", "1.0.0", engine);
  expect(!unknown.has_value(), "unknown package rejected");
  if (!unknown) {
    expect(unknown.error().code == "unknown_style_package", "unknown code");
  }

  auto outdated = registry.check_before_render("editorial", "0.9.0", engine);
  expect(!outdated.has_value(), "outdated revision rejected");
  if (!outdated) {
    expect(outdated.error().code == "outdated_style_package", "outdated code");
  }

  EngineCapabilities old_engine = engine;
  old_engine.engine_version = PackageSemVer{0, 0, 1};
  auto too_old =
      registry.check_before_render("paper-cinema", "1.0.0", old_engine);
  expect(!too_old.has_value(), "engine below minimum rejected");
  if (!too_old) {
    expect(too_old.error().code == "incompatible_style_package",
           "incompatible code");
  }

  EngineCapabilities thin = engine;
  thin.provided = {"path_geometry", "text"};
  auto missing_caps =
      registry.check_before_render("scientific-notebook", "1.0.0", thin);
  expect(!missing_caps.has_value(), "missing capability rejected");
  if (!missing_caps) {
    expect(missing_caps.error().code == "missing_style_capability",
           "missing capability code");
  }

  // Fourth synthetic package: compose existing shared capabilities only. No
  // core-model / render-core change is required to extend the registry.
  StylePackageDesc poster{
      .package_id = "synthetic-poster",
      .version = "1.0.0",
      .minimum_engine_version = "0.1.0",
      .required_capabilities = {"path_geometry", "text", "camera", "export"},
      .registered_components = {"poster_masthead", "poster_rule"},
      .shader_features = {"solid-fill"},
      .material_presets = {"flat_ink"},
  };
  auto registered = registry.register_package(std::move(poster));
  expect(registered.has_value(), "register synthetic fourth package");
  expect(registry.package_ids().size() == 4, "four packages after extension");

  auto poster_ok =
      registry.check_before_render("synthetic-poster", "1.0.0", engine);
  expect(poster_ok.has_value(), "synthetic package ready before render");
  if (poster_ok) {
    expect(poster_ok->registered_components.size() == 2,
           "synthetic components retained");
  }

  auto duplicate = registry.register_package(StylePackageDesc{
      .package_id = "synthetic-poster",
      .version = "1.0.0",
      .minimum_engine_version = "0.1.0",
      .required_capabilities = {"path_geometry"},
  });
  expect(!duplicate.has_value(), "duplicate revision rejected");

  auto bad_semver = registry.register_package(StylePackageDesc{
      .package_id = "broken",
      .version = "01.0.0",
      .minimum_engine_version = "0.1.0",
  });
  expect(!bad_semver.has_value(), "invalid semver rejected");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
