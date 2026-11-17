#include <instantee/render_core/shader_features.hpp>

#include <algorithm>
#include <string_view>
#include <utility>

namespace instantee::render_core {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "shader_features", message};
}

[[nodiscard]] bool valid_name(std::string_view name) noexcept {
  return !name.empty();
}

} // namespace

std::expected<void, diagnostics::Error>
ShaderFeatureRegistry::register_feature(ShaderFeatureDesc descriptor) {
  if (!valid_name(descriptor.id.name)) {
    return std::unexpected(
        make_invalid("invalid_feature_id", "shader feature name is empty"));
  }
  if (descriptor.id.version == 0) {
    return std::unexpected(make_invalid("invalid_feature_version",
                                        "shader feature version must be > 0"));
  }
  for (const ShaderParamDesc &param : descriptor.params) {
    if (!valid_name(param.name)) {
      return std::unexpected(
          make_invalid("invalid_param_name", "shader param name is empty"));
    }
  }
  for (const PassContribution &pass : descriptor.pass_contributions) {
    if (!valid_name(pass.pass_key)) {
      return std::unexpected(
          make_invalid("invalid_pass_key", "pass contribution key is empty"));
    }
  }
  const auto [it, inserted] =
      by_name_.emplace(descriptor.id.name, std::move(descriptor));
  if (!inserted) {
    return std::unexpected(
        make_invalid("duplicate_shader_feature",
                     "shader feature is already registered under this name"));
  }
  (void)it;
  return {};
}

std::expected<ShaderFeatureDesc, diagnostics::Error>
ShaderFeatureRegistry::resolve(std::string_view feature_name) const {
  const auto it = by_name_.find(std::string{feature_name});
  if (it == by_name_.end()) {
    return std::unexpected(make_invalid("unknown_shader_feature",
                                        "shader feature is not registered"));
  }
  return it->second;
}

std::expected<ShaderFeatureDesc, diagnostics::Error>
ShaderFeatureRegistry::resolve(std::string_view feature_name,
                               std::uint32_t version) const {
  auto feature = resolve(feature_name);
  if (!feature) {
    return feature;
  }
  if (feature->id.version != version) {
    return std::unexpected(make_invalid(
        "shader_feature_version_mismatch",
        "registered shader feature version does not match request"));
  }
  return feature;
}

bool ShaderFeatureRegistry::contains(std::string_view feature_name) const {
  return by_name_.contains(std::string{feature_name});
}

std::expected<void, diagnostics::Error>
ShaderFeatureRegistry::validate_material(const MaterialDesc &material) const {
  if (!valid_name(material.id)) {
    return std::unexpected(
        make_invalid("invalid_material_id", "material id is empty"));
  }
  if (material.version == 0) {
    return std::unexpected(make_invalid("invalid_material_version",
                                        "material version must be > 0"));
  }
  for (const std::string &name : material.feature_names) {
    if (!contains(name)) {
      return std::unexpected(
          make_invalid("unknown_shader_feature",
                       "material references an unregistered shader feature"));
    }
  }
  return {};
}

std::expected<ShaderPassPlan, diagnostics::Error>
ShaderFeatureRegistry::plan_passes(
    const MaterialDesc &material,
    const std::vector<std::string> &active_feature_names) const {
  auto valid = validate_material(material);
  if (!valid) {
    return std::unexpected(valid.error());
  }

  std::unordered_map<std::string, bool> active;
  active.reserve(active_feature_names.size());
  for (const std::string &name : active_feature_names) {
    if (!contains(name)) {
      return std::unexpected(make_invalid(
          "unknown_shader_feature",
          "active feature set references an unregistered shader feature"));
    }
    // Ignore actives that are not part of this material.
    if (std::find(material.feature_names.begin(), material.feature_names.end(),
                  name) == material.feature_names.end()) {
      continue;
    }
    active.emplace(name, true);
  }

  struct RankedPass {
    std::uint16_t sort_key = 0;
    std::string pass_key;
  };
  std::vector<RankedPass> ranked;
  for (const std::string &feature_name : material.feature_names) {
    if (!active.contains(feature_name)) {
      continue; // inactive: contributes no passes
    }
    const ShaderFeatureDesc &feature = by_name_.at(feature_name);
    for (const PassContribution &contribution : feature.pass_contributions) {
      ranked.push_back(
          RankedPass{contribution.sort_key, contribution.pass_key});
    }
  }

  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const RankedPass &a, const RankedPass &b) {
                     if (a.sort_key != b.sort_key) {
                       return a.sort_key < b.sort_key;
                     }
                     return a.pass_key < b.pass_key;
                   });

  ShaderPassPlan plan;
  plan.pass_keys.reserve(ranked.size());
  for (const RankedPass &pass : ranked) {
    if (!plan.pass_keys.empty() && plan.pass_keys.back() == pass.pass_key) {
      continue;
    }
    // Deduplicate identical keys while preserving first sort order.
    if (std::find(plan.pass_keys.begin(), plan.pass_keys.end(),
                  pass.pass_key) != plan.pass_keys.end()) {
      continue;
    }
    plan.pass_keys.push_back(pass.pass_key);
  }
  return plan;
}

std::vector<std::string> ShaderFeatureRegistry::names() const {
  std::vector<std::string> out;
  out.reserve(by_name_.size());
  for (const auto &[name, _] : by_name_) {
    out.push_back(name);
  }
  std::sort(out.begin(), out.end());
  return out;
}

ShaderFeatureRegistry make_builtin_shader_feature_registry() {
  ShaderFeatureRegistry registry;

  ShaderFeatureDesc test_pattern;
  test_pattern.id = ShaderFeatureId{.name = "test_pattern", .version = 1};
  test_pattern.params.push_back(ShaderParamDesc{
      .name = "frame_index",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 0.0f},
  });
  test_pattern.pass_contributions.push_back(
      PassContribution{.pass_key = "main_color", .sort_key = 100});
  test_pattern.capabilities =
      ShaderCapabilityNeed{.require_apple7 = true, .require_metal3 = false};
  (void)registry.register_feature(std::move(test_pattern));

  // Empty-pass invariant: registered and activatable, adds zero passes.
  ShaderFeatureDesc noop;
  noop.id = ShaderFeatureId{.name = "noop_effect", .version = 1};
  noop.capabilities.require_apple7 = false;
  (void)registry.register_feature(std::move(noop));

  return registry;
}

} // namespace instantee::render_core
