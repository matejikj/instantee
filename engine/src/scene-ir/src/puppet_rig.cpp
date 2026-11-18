#include <instantee/scene_ir/puppet_rig.hpp>

#include <yyjson.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace instantee::scene_ir {
namespace {

diagnostics::Error make_err(std::string_view code, std::string_view message,
                            std::string_view json_path = {}) {
  auto error = diagnostics::Error{diagnostics::ExitCode::invalid_input,
                                  diagnostics::ErrorKind::invalid_input, code,
                                  "puppet_rig", message};
  if (!json_path.empty()) {
    error = std::move(error).with_json_path(std::string(json_path));
  }
  return error;
}

std::string path_join(std::string_view base, std::string_view child) {
  std::string out;
  out.reserve(base.size() + child.size() + 1U);
  out.append(base);
  if (!child.empty() && child.front() != '/') {
    out.push_back('/');
  }
  out.append(child);
  return out;
}

std::string path_index(std::string_view base, std::size_t index) {
  return path_join(base, std::to_string(index));
}

bool key_allowed(std::string_view key,
                 std::span<const std::string_view> allowed) {
  return std::find(allowed.begin(), allowed.end(), key) != allowed.end();
}

std::expected<void, diagnostics::Error>
reject_unknown_keys(yyjson_val *obj, std::string_view json_path,
                    std::span<const std::string_view> allowed) {
  yyjson_obj_iter iter;
  yyjson_obj_iter_init(obj, &iter);
  yyjson_val *key;
  while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
    const char *text = yyjson_get_str(key);
    const std::string_view name = text != nullptr ? text : "";
    if (!key_allowed(name, allowed)) {
      return std::unexpected(make_err("unknown_field", "Unknown object field",
                                      path_join(json_path, name)));
    }
  }
  return {};
}

std::expected<yyjson_val *, diagnostics::Error>
require_obj(yyjson_val *parent, std::string_view key, std::string_view path) {
  yyjson_val *value = yyjson_obj_getn(parent, key.data(), key.size());
  if (value == nullptr) {
    return std::unexpected(make_err(
        "missing_field", "Required field is missing", path_join(path, key)));
  }
  if (!yyjson_is_obj(value)) {
    return std::unexpected(
        make_err("type_mismatch", "Expected object", path_join(path, key)));
  }
  return value;
}

std::expected<yyjson_val *, diagnostics::Error>
require_arr(yyjson_val *parent, std::string_view key, std::string_view path) {
  yyjson_val *value = yyjson_obj_getn(parent, key.data(), key.size());
  if (value == nullptr) {
    return std::unexpected(make_err(
        "missing_field", "Required field is missing", path_join(path, key)));
  }
  if (!yyjson_is_arr(value)) {
    return std::unexpected(
        make_err("type_mismatch", "Expected array", path_join(path, key)));
  }
  return value;
}

std::expected<std::string, diagnostics::Error>
require_string(yyjson_val *parent, std::string_view key, std::string_view path,
               std::size_t max_bytes) {
  yyjson_val *value = yyjson_obj_getn(parent, key.data(), key.size());
  if (value == nullptr) {
    return std::unexpected(make_err(
        "missing_field", "Required field is missing", path_join(path, key)));
  }
  if (!yyjson_is_str(value)) {
    return std::unexpected(
        make_err("type_mismatch", "Expected string", path_join(path, key)));
  }
  const char *text = yyjson_get_str(value);
  const std::string_view view = text != nullptr ? text : "";
  if (view.empty() || view.size() > max_bytes ||
      view.find('/') != std::string_view::npos ||
      view.find('\\') != std::string_view::npos) {
    return std::unexpected(
        make_err("invalid_id", "Invalid identifier", path_join(path, key)));
  }
  return std::string(view);
}

std::expected<double, diagnostics::Error>
require_number(yyjson_val *parent, std::string_view key,
               std::string_view path) {
  yyjson_val *value = yyjson_obj_getn(parent, key.data(), key.size());
  if (value == nullptr) {
    return std::unexpected(make_err(
        "missing_field", "Required field is missing", path_join(path, key)));
  }
  if (!yyjson_is_num(value)) {
    return std::unexpected(
        make_err("type_mismatch", "Expected number", path_join(path, key)));
  }
  const double number = yyjson_get_num(value);
  if (!std::isfinite(number)) {
    return std::unexpected(
        make_err("non_finite", "Expected finite number", path_join(path, key)));
  }
  return number;
}

std::expected<std::uint32_t, diagnostics::Error>
optional_u32(yyjson_val *parent, std::string_view key, std::string_view path,
             std::uint32_t fallback) {
  yyjson_val *value = yyjson_obj_getn(parent, key.data(), key.size());
  if (value == nullptr) {
    return fallback;
  }
  if (!yyjson_is_int(value) && !yyjson_is_uint(value)) {
    return std::unexpected(
        make_err("type_mismatch", "Expected integer", path_join(path, key)));
  }
  const std::int64_t raw = yyjson_get_sint(value);
  if (raw < 0 || raw > static_cast<std::int64_t>(
                           std::numeric_limits<std::uint32_t>::max())) {
    return std::unexpected(
        make_err("out_of_range", "Integer out of range", path_join(path, key)));
  }
  return static_cast<std::uint32_t>(raw);
}

std::expected<PuppetVec2, diagnostics::Error>
parse_vec2(yyjson_val *obj, std::string_view path) {
  static constexpr std::string_view kAllowed[] = {"x", "y"};
  if (auto ok = reject_unknown_keys(obj, path, kAllowed); !ok) {
    return std::unexpected(ok.error());
  }
  auto x = require_number(obj, "x", path);
  if (!x) {
    return std::unexpected(x.error());
  }
  auto y = require_number(obj, "y", path);
  if (!y) {
    return std::unexpected(y.error());
  }
  return PuppetVec2{*x, *y};
}

std::expected<PuppetJoint, diagnostics::Error>
parse_joint(yyjson_val *obj, std::string_view path,
            const PuppetRigLimits &limits) {
  static constexpr std::string_view kAllowed[] = {"id", "parent_id", "pivot"};
  if (auto ok = reject_unknown_keys(obj, path, kAllowed); !ok) {
    return std::unexpected(ok.error());
  }
  PuppetJoint joint;
  auto id = require_string(obj, "id", path, limits.max_string_bytes);
  if (!id) {
    return std::unexpected(id.error());
  }
  joint.id = std::move(*id);
  if (yyjson_obj_get(obj, "parent_id") != nullptr) {
    auto parent =
        require_string(obj, "parent_id", path, limits.max_string_bytes);
    if (!parent) {
      return std::unexpected(parent.error());
    }
    joint.parent_id = std::move(*parent);
  }
  auto pivot_obj = require_obj(obj, "pivot", path);
  if (!pivot_obj) {
    return std::unexpected(pivot_obj.error());
  }
  auto pivot = parse_vec2(*pivot_obj, path_join(path, "pivot"));
  if (!pivot) {
    return std::unexpected(pivot.error());
  }
  joint.pivot = *pivot;
  return joint;
}

std::expected<PuppetBone, diagnostics::Error>
parse_bone(yyjson_val *obj, std::string_view path,
           const PuppetRigLimits &limits) {
  static constexpr std::string_view kAllowed[] = {"id", "from_joint_id",
                                                  "to_joint_id"};
  if (auto ok = reject_unknown_keys(obj, path, kAllowed); !ok) {
    return std::unexpected(ok.error());
  }
  PuppetBone bone;
  auto id = require_string(obj, "id", path, limits.max_string_bytes);
  if (!id) {
    return std::unexpected(id.error());
  }
  bone.id = std::move(*id);
  auto from =
      require_string(obj, "from_joint_id", path, limits.max_string_bytes);
  if (!from) {
    return std::unexpected(from.error());
  }
  bone.from_joint_id = std::move(*from);
  auto to = require_string(obj, "to_joint_id", path, limits.max_string_bytes);
  if (!to) {
    return std::unexpected(to.error());
  }
  bone.to_joint_id = std::move(*to);
  return bone;
}

std::expected<PuppetLayer, diagnostics::Error>
parse_layer(yyjson_val *obj, std::string_view path,
            const PuppetRigLimits &limits) {
  static constexpr std::string_view kAllowed[] = {"id", "bind_joint_id"};
  if (auto ok = reject_unknown_keys(obj, path, kAllowed); !ok) {
    return std::unexpected(ok.error());
  }
  PuppetLayer layer;
  auto id = require_string(obj, "id", path, limits.max_string_bytes);
  if (!id) {
    return std::unexpected(id.error());
  }
  layer.id = std::move(*id);
  auto bind =
      require_string(obj, "bind_joint_id", path, limits.max_string_bytes);
  if (!bind) {
    return std::unexpected(bind.error());
  }
  layer.bind_joint_id = std::move(*bind);
  return layer;
}

std::expected<PuppetSkinBinding, diagnostics::Error>
parse_skin(yyjson_val *obj, std::string_view path,
           const PuppetRigLimits &limits) {
  static constexpr std::string_view kAllowed[] = {"layer_id", "influences"};
  if (auto ok = reject_unknown_keys(obj, path, kAllowed); !ok) {
    return std::unexpected(ok.error());
  }
  PuppetSkinBinding binding;
  auto layer_id =
      require_string(obj, "layer_id", path, limits.max_string_bytes);
  if (!layer_id) {
    return std::unexpected(layer_id.error());
  }
  binding.layer_id = std::move(*layer_id);
  auto influences = require_arr(obj, "influences", path);
  if (!influences) {
    return std::unexpected(influences.error());
  }
  const std::size_t count = yyjson_arr_size(*influences);
  if (count == 0U || count > limits.max_influences) {
    return std::unexpected(make_err("budget_exceeded",
                                    "Influence count out of range",
                                    path_join(path, "influences")));
  }
  binding.influences.reserve(count);
  std::size_t index = 0;
  yyjson_arr_iter iter;
  yyjson_arr_iter_init(*influences, &iter);
  yyjson_val *item;
  while ((item = yyjson_arr_iter_next(&iter)) != nullptr) {
    const std::string item_path =
        path_index(path_join(path, "influences"), index++);
    if (!yyjson_is_obj(item)) {
      return std::unexpected(
          make_err("type_mismatch", "Expected object", item_path));
    }
    static constexpr std::string_view kInfAllowed[] = {"joint_id", "weight"};
    if (auto ok = reject_unknown_keys(item, item_path, kInfAllowed); !ok) {
      return std::unexpected(ok.error());
    }
    PuppetSkinInfluence influence;
    auto joint =
        require_string(item, "joint_id", item_path, limits.max_string_bytes);
    if (!joint) {
      return std::unexpected(joint.error());
    }
    influence.joint_id = std::move(*joint);
    auto weight = require_number(item, "weight", item_path);
    if (!weight) {
      return std::unexpected(weight.error());
    }
    if (!(*weight > 0.0) || *weight > 1.0) {
      return std::unexpected(make_err("out_of_range", "Weight out of range",
                                      path_join(item_path, "weight")));
    }
    influence.weight = *weight;
    binding.influences.push_back(std::move(influence));
  }
  return binding;
}

std::expected<PuppetConstraint, diagnostics::Error>
parse_constraint(yyjson_val *obj, std::string_view path,
                 const PuppetRigLimits &limits) {
  static constexpr std::string_view kAllowed[] = {"id", "type", "joint_id",
                                                  "min_rad", "max_rad"};
  if (auto ok = reject_unknown_keys(obj, path, kAllowed); !ok) {
    return std::unexpected(ok.error());
  }
  PuppetConstraint constraint;
  auto id = require_string(obj, "id", path, limits.max_string_bytes);
  if (!id) {
    return std::unexpected(id.error());
  }
  constraint.id = std::move(*id);
  auto type = require_string(obj, "type", path, limits.max_string_bytes);
  if (!type) {
    return std::unexpected(type.error());
  }
  if (*type == "parent") {
    constraint.type = PuppetConstraintType::parent;
  } else if (*type == "hinge_limit") {
    constraint.type = PuppetConstraintType::hinge_limit;
  } else {
    return std::unexpected(make_err("unsupported_constraint",
                                    "Constraint type is not in paper_2d_v1",
                                    path_join(path, "type")));
  }
  auto joint = require_string(obj, "joint_id", path, limits.max_string_bytes);
  if (!joint) {
    return std::unexpected(joint.error());
  }
  constraint.joint_id = std::move(*joint);
  const bool has_min = yyjson_obj_get(obj, "min_rad") != nullptr;
  const bool has_max = yyjson_obj_get(obj, "max_rad") != nullptr;
  if (constraint.type == PuppetConstraintType::hinge_limit) {
    if (!has_min || !has_max) {
      return std::unexpected(make_err(
          "missing_field", "hinge_limit requires min_rad and max_rad", path));
    }
    auto min_rad = require_number(obj, "min_rad", path);
    if (!min_rad) {
      return std::unexpected(min_rad.error());
    }
    auto max_rad = require_number(obj, "max_rad", path);
    if (!max_rad) {
      return std::unexpected(max_rad.error());
    }
    constraint.min_rad = *min_rad;
    constraint.max_rad = *max_rad;
  } else if (has_min || has_max) {
    return std::unexpected(
        make_err("unexpected_field",
                 "parent constraint must not set hinge limits", path));
  }
  return constraint;
}

std::expected<PuppetReferencePose, diagnostics::Error>
parse_pose(yyjson_val *obj, std::string_view path,
           const PuppetRigLimits &limits) {
  static constexpr std::string_view kAllowed[] = {"id", "joints"};
  if (auto ok = reject_unknown_keys(obj, path, kAllowed); !ok) {
    return std::unexpected(ok.error());
  }
  PuppetReferencePose pose;
  auto id = require_string(obj, "id", path, limits.max_string_bytes);
  if (!id) {
    return std::unexpected(id.error());
  }
  pose.id = std::move(*id);
  auto joints = require_arr(obj, "joints", path);
  if (!joints) {
    return std::unexpected(joints.error());
  }
  const std::size_t count = yyjson_arr_size(*joints);
  if (count == 0U || count > limits.max_joints) {
    return std::unexpected(make_err("budget_exceeded",
                                    "Pose joint count out of range",
                                    path_join(path, "joints")));
  }
  pose.joints.reserve(count);
  std::size_t index = 0;
  yyjson_arr_iter iter;
  yyjson_arr_iter_init(*joints, &iter);
  yyjson_val *item;
  while ((item = yyjson_arr_iter_next(&iter)) != nullptr) {
    const std::string item_path =
        path_index(path_join(path, "joints"), index++);
    if (!yyjson_is_obj(item)) {
      return std::unexpected(
          make_err("type_mismatch", "Expected object", item_path));
    }
    static constexpr std::string_view kPoseAllowed[] = {
        "joint_id", "rotation_z_rad", "translate"};
    if (auto ok = reject_unknown_keys(item, item_path, kPoseAllowed); !ok) {
      return std::unexpected(ok.error());
    }
    PuppetPoseJoint entry;
    auto joint =
        require_string(item, "joint_id", item_path, limits.max_string_bytes);
    if (!joint) {
      return std::unexpected(joint.error());
    }
    entry.joint_id = std::move(*joint);
    auto rotation = require_number(item, "rotation_z_rad", item_path);
    if (!rotation) {
      return std::unexpected(rotation.error());
    }
    entry.rotation_z_rad = *rotation;
    if (yyjson_obj_get(item, "translate") != nullptr) {
      auto translate_obj = require_obj(item, "translate", item_path);
      if (!translate_obj) {
        return std::unexpected(translate_obj.error());
      }
      auto translate =
          parse_vec2(*translate_obj, path_join(item_path, "translate"));
      if (!translate) {
        return std::unexpected(translate.error());
      }
      entry.translate = *translate;
    }
    pose.joints.push_back(std::move(entry));
  }
  return pose;
}

template <typename T, typename ParseFn>
std::expected<std::vector<T>, diagnostics::Error>
parse_array(yyjson_val *root, std::string_view key, std::string_view path,
            std::size_t max_items, ParseFn parse_one, bool required) {
  yyjson_val *value = yyjson_obj_getn(root, key.data(), key.size());
  if (value == nullptr) {
    if (required) {
      return std::unexpected(make_err(
          "missing_field", "Required field is missing", path_join(path, key)));
    }
    return std::vector<T>{};
  }
  if (!yyjson_is_arr(value)) {
    return std::unexpected(
        make_err("type_mismatch", "Expected array", path_join(path, key)));
  }
  const std::size_t count = yyjson_arr_size(value);
  if (count > max_items) {
    return std::unexpected(make_err("budget_exceeded", "Array exceeds budget",
                                    path_join(path, key)));
  }
  std::vector<T> out;
  out.reserve(count);
  std::size_t index = 0;
  yyjson_arr_iter iter;
  yyjson_arr_iter_init(value, &iter);
  yyjson_val *item;
  while ((item = yyjson_arr_iter_next(&iter)) != nullptr) {
    const std::string item_path = path_index(path_join(path, key), index++);
    if (!yyjson_is_obj(item)) {
      return std::unexpected(
          make_err("type_mismatch", "Expected object", item_path));
    }
    auto parsed = parse_one(item, item_path);
    if (!parsed) {
      return std::unexpected(parsed.error());
    }
    out.push_back(std::move(*parsed));
  }
  return out;
}

bool insert_unique(std::unordered_set<std::string> &seen, const std::string &id,
                   std::string_view path, diagnostics::Error &error) {
  if (!seen.insert(id).second) {
    error = make_err("duplicate_id", "Duplicate identifier", std::string(path));
    return false;
  }
  return true;
}

} // namespace

std::expected<PuppetRigDocument, diagnostics::Error>
parse_puppet_rig_json(std::string_view json_text,
                      const PuppetRigLimits &limits) {
  if (json_text.size() > limits.max_bytes) {
    return std::unexpected(
        make_err("budget_exceeded", "Rig document exceeds byte budget"));
  }

  yyjson_read_err read_err{};
  yyjson_doc *doc =
      yyjson_read_opts(const_cast<char *>(json_text.data()), json_text.size(),
                       YYJSON_READ_NOFLAG, nullptr, &read_err);
  if (doc == nullptr) {
    return std::unexpected(make_err("json_parse", read_err.msg != nullptr
                                                      ? read_err.msg
                                                      : "JSON parse failed"));
  }

  struct DocGuard {
    yyjson_doc *handle;
    ~DocGuard() { yyjson_doc_free(handle); }
  } guard{doc};

  yyjson_val *root = yyjson_doc_get_root(doc);
  if (root == nullptr || !yyjson_is_obj(root)) {
    return std::unexpected(make_err("type_mismatch", "Root must be an object"));
  }

  static constexpr std::string_view kRootAllowed[] = {
      "schema_version", "id",          "retarget",
      "joints",         "bones",       "layers",
      "skin_bindings",  "constraints", "reference_poses"};
  if (auto ok = reject_unknown_keys(root, "$", kRootAllowed); !ok) {
    return std::unexpected(ok.error());
  }

  PuppetRigDocument out;
  auto schema =
      require_string(root, "schema_version", "$", limits.max_string_bytes);
  if (!schema) {
    return std::unexpected(schema.error());
  }
  if (*schema != kPuppetRigSchemaVersion) {
    return std::unexpected(make_err("incompatible_schema",
                                    "Unsupported puppet-rig schema_version",
                                    "$/schema_version"));
  }
  out.schema_version = std::move(*schema);

  auto id = require_string(root, "id", "$", limits.max_string_bytes);
  if (!id) {
    return std::unexpected(id.error());
  }
  out.id = std::move(*id);

  auto retarget_obj = require_obj(root, "retarget", "$");
  if (!retarget_obj) {
    return std::unexpected(retarget_obj.error());
  }
  static constexpr std::string_view kRetargetAllowed[] = {
      "profile", "space", "max_joints", "max_influences_per_vertex"};
  if (auto ok =
          reject_unknown_keys(*retarget_obj, "$/retarget", kRetargetAllowed);
      !ok) {
    return std::unexpected(ok.error());
  }
  auto profile = require_string(*retarget_obj, "profile", "$/retarget",
                                limits.max_string_bytes);
  if (!profile) {
    return std::unexpected(profile.error());
  }
  if (*profile != kPuppetRigRetargetProfile) {
    return std::unexpected(make_err("unsupported_retarget",
                                    "Only paper_2d_v1 retarget is supported",
                                    "$/retarget/profile"));
  }
  out.retarget.profile = std::move(*profile);
  auto space = require_string(*retarget_obj, "space", "$/retarget",
                              limits.max_string_bytes);
  if (!space) {
    return std::unexpected(space.error());
  }
  if (*space != kPuppetRigRetargetSpace) {
    return std::unexpected(make_err("unsupported_retarget",
                                    "Only 2d retarget space is supported",
                                    "$/retarget/space"));
  }
  out.retarget.space = std::move(*space);
  auto max_joints =
      optional_u32(*retarget_obj, "max_joints", "$/retarget", 64U);
  if (!max_joints) {
    return std::unexpected(max_joints.error());
  }
  if (*max_joints < 1U ||
      *max_joints > static_cast<std::uint32_t>(limits.max_joints)) {
    return std::unexpected(make_err("out_of_range", "max_joints out of range",
                                    "$/retarget/max_joints"));
  }
  out.retarget.max_joints = *max_joints;
  auto max_inf = optional_u32(*retarget_obj, "max_influences_per_vertex",
                              "$/retarget", 4U);
  if (!max_inf) {
    return std::unexpected(max_inf.error());
  }
  if (*max_inf < 1U ||
      *max_inf > static_cast<std::uint32_t>(limits.max_influences)) {
    return std::unexpected(make_err("out_of_range",
                                    "max_influences_per_vertex out of range",
                                    "$/retarget/max_influences_per_vertex"));
  }
  out.retarget.max_influences_per_vertex = *max_inf;

  auto joints = parse_array<PuppetJoint>(
      root, "joints", "$", limits.max_joints,
      [&](yyjson_val *item, std::string_view item_path) {
        return parse_joint(item, item_path, limits);
      },
      true);
  if (!joints) {
    return std::unexpected(joints.error());
  }
  if (joints->empty()) {
    return std::unexpected(
        make_err("missing_field", "joints must be non-empty", "$/joints"));
  }
  out.joints = std::move(*joints);

  auto bones = parse_array<PuppetBone>(
      root, "bones", "$", limits.max_bones,
      [&](yyjson_val *item, std::string_view item_path) {
        return parse_bone(item, item_path, limits);
      },
      false);
  if (!bones) {
    return std::unexpected(bones.error());
  }
  out.bones = std::move(*bones);

  auto layers = parse_array<PuppetLayer>(
      root, "layers", "$", limits.max_layers,
      [&](yyjson_val *item, std::string_view item_path) {
        return parse_layer(item, item_path, limits);
      },
      false);
  if (!layers) {
    return std::unexpected(layers.error());
  }
  out.layers = std::move(*layers);

  auto skins = parse_array<PuppetSkinBinding>(
      root, "skin_bindings", "$", limits.max_skin_bindings,
      [&](yyjson_val *item, std::string_view item_path) {
        return parse_skin(item, item_path, limits);
      },
      false);
  if (!skins) {
    return std::unexpected(skins.error());
  }
  out.skin_bindings = std::move(*skins);

  auto constraints = parse_array<PuppetConstraint>(
      root, "constraints", "$", limits.max_constraints,
      [&](yyjson_val *item, std::string_view item_path) {
        return parse_constraint(item, item_path, limits);
      },
      false);
  if (!constraints) {
    return std::unexpected(constraints.error());
  }
  out.constraints = std::move(*constraints);

  auto poses = parse_array<PuppetReferencePose>(
      root, "reference_poses", "$", limits.max_poses,
      [&](yyjson_val *item, std::string_view item_path) {
        return parse_pose(item, item_path, limits);
      },
      false);
  if (!poses) {
    return std::unexpected(poses.error());
  }
  out.reference_poses = std::move(*poses);
  return out;
}

std::expected<void, diagnostics::Error>
validate_puppet_rig(const PuppetRigDocument &document,
                    const PuppetRigLimits &limits) {
  if (document.schema_version != kPuppetRigSchemaVersion) {
    return std::unexpected(make_err("incompatible_schema",
                                    "Unsupported puppet-rig schema_version"));
  }
  if (document.retarget.profile != kPuppetRigRetargetProfile ||
      document.retarget.space != kPuppetRigRetargetSpace) {
    return std::unexpected(
        make_err("unsupported_retarget", "Only paper_2d_v1 / 2d is supported"));
  }
  if (document.joints.empty() || document.joints.size() > limits.max_joints) {
    return std::unexpected(
        make_err("budget_exceeded", "Joint count out of range"));
  }
  if (document.joints.size() >
      static_cast<std::size_t>(document.retarget.max_joints)) {
    return std::unexpected(
        make_err("budget_exceeded", "Joint count exceeds retarget.max_joints"));
  }
  if (document.bones.size() > limits.max_bones ||
      document.layers.size() > limits.max_layers ||
      document.skin_bindings.size() > limits.max_skin_bindings ||
      document.constraints.size() > limits.max_constraints ||
      document.reference_poses.size() > limits.max_poses) {
    return std::unexpected(
        make_err("budget_exceeded", "Rig collection exceeds budget"));
  }

  std::unordered_set<std::string> joint_ids;
  std::unordered_map<std::string, std::optional<std::string>> parents;
  joint_ids.reserve(document.joints.size());
  parents.reserve(document.joints.size());
  diagnostics::Error error;
  std::size_t root_count = 0;
  for (std::size_t i = 0; i < document.joints.size(); ++i) {
    const auto &joint = document.joints[i];
    if (!insert_unique(joint_ids, joint.id, path_index("$/joints", i) + "/id",
                       error)) {
      return std::unexpected(error);
    }
    if (!joint.parent_id.has_value()) {
      ++root_count;
    }
    parents.emplace(joint.id, joint.parent_id);
  }
  if (root_count != 1U) {
    return std::unexpected(
        make_err("invalid_hierarchy", "Rig must have exactly one root joint"));
  }
  for (std::size_t i = 0; i < document.joints.size(); ++i) {
    const auto &joint = document.joints[i];
    if (!joint.parent_id.has_value()) {
      continue;
    }
    if (!joint_ids.contains(*joint.parent_id)) {
      return std::unexpected(
          make_err("dangling_ref", "parent_id does not name a joint",
                   path_index("$/joints", i) + "/parent_id"));
    }
    std::unordered_set<std::string> seen;
    std::string cursor = joint.id;
    while (true) {
      if (!seen.insert(cursor).second) {
        return std::unexpected(make_err("cyclic_parent",
                                        "Joint parent chain contains a cycle",
                                        path_index("$/joints", i)));
      }
      const auto it = parents.find(cursor);
      if (it == parents.end() || !it->second.has_value()) {
        break;
      }
      cursor = *it->second;
    }
  }

  std::unordered_set<std::string> bone_ids;
  for (std::size_t i = 0; i < document.bones.size(); ++i) {
    const auto &bone = document.bones[i];
    if (!insert_unique(bone_ids, bone.id, path_index("$/bones", i) + "/id",
                       error)) {
      return std::unexpected(error);
    }
    if (!joint_ids.contains(bone.from_joint_id) ||
        !joint_ids.contains(bone.to_joint_id)) {
      return std::unexpected(make_err("dangling_ref",
                                      "Bone references unknown joint",
                                      path_index("$/bones", i)));
    }
    if (bone.from_joint_id == bone.to_joint_id) {
      return std::unexpected(make_err("invalid_bone",
                                      "Bone endpoints must differ",
                                      path_index("$/bones", i)));
    }
  }

  std::unordered_set<std::string> layer_ids;
  for (std::size_t i = 0; i < document.layers.size(); ++i) {
    const auto &layer = document.layers[i];
    if (!insert_unique(layer_ids, layer.id, path_index("$/layers", i) + "/id",
                       error)) {
      return std::unexpected(error);
    }
    if (!joint_ids.contains(layer.bind_joint_id)) {
      return std::unexpected(
          make_err("dangling_ref", "Layer bind_joint_id is unknown",
                   path_index("$/layers", i) + "/bind_joint_id"));
    }
  }

  std::unordered_set<std::string> skin_layers;
  for (std::size_t i = 0; i < document.skin_bindings.size(); ++i) {
    const auto &binding = document.skin_bindings[i];
    if (!layer_ids.contains(binding.layer_id)) {
      return std::unexpected(
          make_err("dangling_ref", "skin_bindings.layer_id is unknown",
                   path_index("$/skin_bindings", i) + "/layer_id"));
    }
    if (!skin_layers.insert(binding.layer_id).second) {
      return std::unexpected(
          make_err("duplicate_id", "Duplicate skin binding for layer",
                   path_index("$/skin_bindings", i) + "/layer_id"));
    }
    if (binding.influences.empty() ||
        binding.influences.size() > limits.max_influences ||
        binding.influences.size() >
            static_cast<std::size_t>(
                document.retarget.max_influences_per_vertex)) {
      return std::unexpected(
          make_err("budget_exceeded", "Influence count out of range",
                   path_index("$/skin_bindings", i) + "/influences"));
    }
    double sum = 0.0;
    std::unordered_set<std::string> influence_joints;
    for (std::size_t j = 0; j < binding.influences.size(); ++j) {
      const auto &inf = binding.influences[j];
      if (!joint_ids.contains(inf.joint_id)) {
        return std::unexpected(make_err(
            "dangling_ref", "Skin influence joint is unknown",
            path_index(path_index("$/skin_bindings", i) + "/influences", j) +
                "/joint_id"));
      }
      if (!influence_joints.insert(inf.joint_id).second) {
        return std::unexpected(make_err(
            "duplicate_id", "Duplicate influence joint",
            path_index(path_index("$/skin_bindings", i) + "/influences", j) +
                "/joint_id"));
      }
      if (!(inf.weight > 0.0) || inf.weight > 1.0 ||
          !std::isfinite(inf.weight)) {
        return std::unexpected(make_err(
            "out_of_range", "Skin weight out of range",
            path_index(path_index("$/skin_bindings", i) + "/influences", j) +
                "/weight"));
      }
      sum += inf.weight;
    }
    if (!std::isfinite(sum) || std::abs(sum - 1.0) > 1e-6) {
      return std::unexpected(make_err(
          "invalid_skin_weights", "Skin influence weights must sum to 1",
          path_index("$/skin_bindings", i) + "/influences"));
    }
  }

  std::unordered_set<std::string> constraint_ids;
  for (std::size_t i = 0; i < document.constraints.size(); ++i) {
    const auto &constraint = document.constraints[i];
    if (!insert_unique(constraint_ids, constraint.id,
                       path_index("$/constraints", i) + "/id", error)) {
      return std::unexpected(error);
    }
    if (!joint_ids.contains(constraint.joint_id)) {
      return std::unexpected(
          make_err("dangling_ref", "Constraint joint_id is unknown",
                   path_index("$/constraints", i) + "/joint_id"));
    }
    if (constraint.type == PuppetConstraintType::hinge_limit) {
      if (!constraint.min_rad.has_value() || !constraint.max_rad.has_value()) {
        return std::unexpected(make_err(
            "missing_field", "hinge_limit requires min_rad and max_rad",
            path_index("$/constraints", i)));
      }
      if (!std::isfinite(*constraint.min_rad) ||
          !std::isfinite(*constraint.max_rad) ||
          *constraint.min_rad > *constraint.max_rad) {
        return std::unexpected(make_err(
            "invalid_hinge", "hinge_limit requires finite min_rad <= max_rad",
            path_index("$/constraints", i)));
      }
    } else if (constraint.min_rad.has_value() ||
               constraint.max_rad.has_value()) {
      return std::unexpected(make_err(
          "unexpected_field", "parent constraint must not set hinge limits",
          path_index("$/constraints", i)));
    }
  }

  std::unordered_set<std::string> pose_ids;
  for (std::size_t i = 0; i < document.reference_poses.size(); ++i) {
    const auto &pose = document.reference_poses[i];
    if (!insert_unique(pose_ids, pose.id,
                       path_index("$/reference_poses", i) + "/id", error)) {
      return std::unexpected(error);
    }
    if (pose.joints.empty() || pose.joints.size() > limits.max_joints) {
      return std::unexpected(
          make_err("budget_exceeded", "Pose joint count out of range",
                   path_index("$/reference_poses", i) + "/joints"));
    }
    std::unordered_set<std::string> pose_joints;
    for (std::size_t j = 0; j < pose.joints.size(); ++j) {
      const auto &entry = pose.joints[j];
      if (!joint_ids.contains(entry.joint_id)) {
        return std::unexpected(make_err(
            "dangling_ref", "Pose joint_id is unknown",
            path_index(path_index("$/reference_poses", i) + "/joints", j) +
                "/joint_id"));
      }
      if (!pose_joints.insert(entry.joint_id).second) {
        return std::unexpected(make_err(
            "duplicate_id", "Duplicate joint in reference pose",
            path_index(path_index("$/reference_poses", i) + "/joints", j) +
                "/joint_id"));
      }
      if (!std::isfinite(entry.rotation_z_rad)) {
        return std::unexpected(make_err(
            "non_finite", "Pose rotation must be finite",
            path_index(path_index("$/reference_poses", i) + "/joints", j) +
                "/rotation_z_rad"));
      }
      if (entry.translate.has_value() && (!std::isfinite(entry.translate->x) ||
                                          !std::isfinite(entry.translate->y))) {
        return std::unexpected(make_err(
            "non_finite", "Pose translate must be finite",
            path_index(path_index("$/reference_poses", i) + "/joints", j) +
                "/translate"));
      }
    }
  }

  return {};
}

std::expected<PuppetRigDocument, diagnostics::Error>
load_puppet_rig_json(std::string_view json_text,
                     const PuppetRigLimits &limits) {
  auto parsed = parse_puppet_rig_json(json_text, limits);
  if (!parsed) {
    return parsed;
  }
  if (auto ok = validate_puppet_rig(*parsed, limits); !ok) {
    return std::unexpected(ok.error());
  }
  return parsed;
}

} // namespace instantee::scene_ir
