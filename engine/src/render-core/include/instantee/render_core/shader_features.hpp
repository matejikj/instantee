#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace instantee::render_core {

// Versioned feature identity. Styles register features by id; render-core never
// branches on style package names.
struct ShaderFeatureId {
  std::string name;
  std::uint32_t version = 1;
};

enum class ShaderParamKind : std::uint8_t {
  boolean = 1,
  scalar_f32 = 2,
  color_rgba = 3,
};

struct ShaderParamDefault {
  ShaderParamKind kind = ShaderParamKind::boolean;
  bool boolean_value = false;
  float scalar_f32 = 0.0f;
  Rgba8 color_rgba{};
};

struct ShaderParamDesc {
  std::string name;
  ShaderParamDefault default_value{};
};

// Logical pass contribution. An empty contribution list is the empty-pass
// invariant: activating the feature adds no GPU pass.
struct PassContribution {
  std::string pass_key;
  std::uint16_t sort_key = 0;
};

// Abstract capability needs checked by backends before enabling a feature.
struct ShaderCapabilityNeed {
  bool require_apple7 = true;
  bool require_metal3 = false;
};

struct ShaderFeatureDesc {
  ShaderFeatureId id{};
  std::vector<ShaderParamDesc> params{};
  std::vector<PassContribution> pass_contributions{};
  ShaderCapabilityNeed capabilities{};
};

// Material binds an ordered set of feature ids. Unknown ids fail validation.
struct MaterialDesc {
  std::string id;
  std::uint32_t version = 1;
  std::vector<std::string> feature_names{};
};

// Planned passes after filtering inactive/empty features. Keys are unique and
// sorted by (sort_key, pass_key).
struct ShaderPassPlan {
  std::vector<std::string> pass_keys{};
};

class ShaderFeatureRegistry {
public:
  [[nodiscard]] std::expected<void, diagnostics::Error>
  register_feature(ShaderFeatureDesc descriptor);

  [[nodiscard]] std::expected<ShaderFeatureDesc, diagnostics::Error>
  resolve(std::string_view feature_name) const;

  [[nodiscard]] std::expected<ShaderFeatureDesc, diagnostics::Error>
  resolve(std::string_view feature_name, std::uint32_t version) const;

  [[nodiscard]] bool contains(std::string_view feature_name) const;

  // Validates that every referenced feature exists. Does not plan passes.
  [[nodiscard]] std::expected<void, diagnostics::Error>
  validate_material(const MaterialDesc &material) const;

  // Builds the pass list for the active subset of a material. Features absent
  // from `active_feature_names` contribute nothing (inactive). Features with
  // empty pass_contributions also contribute nothing (empty-pass invariant).
  // Unknown names fail.
  [[nodiscard]] std::expected<ShaderPassPlan, diagnostics::Error>
  plan_passes(const MaterialDesc &material,
              const std::vector<std::string> &active_feature_names) const;

  [[nodiscard]] std::vector<std::string> names() const;

private:
  std::unordered_map<std::string, ShaderFeatureDesc> by_name_;
};

// Builtins cover the offline test-pattern module and an intentionally empty
// pass feature used to prove the empty-pass invariant.
[[nodiscard]] ShaderFeatureRegistry make_builtin_shader_feature_registry();

} // namespace instantee::render_core
