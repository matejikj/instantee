#include <instantee/scene_ir/authoring.hpp>
#include <instantee/scene_ir/load.hpp>
#include <instantee/scene_ir/migrate.hpp>

#include <instantee/core_model/integer_encoding.hpp>

#include <yyjson.h>

#include <cmath>
#include <string>
#include <utility>

namespace instantee::scene_ir {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message,
                       std::string path) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "authoring", message}
      .with_json_path(std::move(path));
}

std::string join(std::string_view base, std::string_view child) {
  std::string out(base);
  if (!child.empty() && (out.empty() || out.back() != '/')) {
    out.push_back('/');
  }
  out.append(child);
  return out;
}

struct DocGuard {
  yyjson_doc *doc = nullptr;
  ~DocGuard() { yyjson_doc_free(doc); }
  DocGuard(const DocGuard &) = delete;
  DocGuard &operator=(const DocGuard &) = delete;
  explicit DocGuard(yyjson_doc *owned) : doc(owned) {}
};

std::expected<std::string, diagnostics::Error>
req_str(yyjson_val *obj, std::string_view key, std::string_view path) {
  yyjson_val *value = yyjson_obj_getn(obj, key.data(), key.size());
  if (value == nullptr || !yyjson_is_str(value)) {
    return std::unexpected(err("missing_field",
                               "Required string field missing or wrong type",
                               join(path, key)));
  }
  return std::string(yyjson_get_str(value), yyjson_get_len(value));
}

std::expected<std::uint64_t, diagnostics::Error>
req_uint(yyjson_val *obj, std::string_view key, std::string_view path,
         std::uint64_t min_v, std::uint64_t max_v) {
  yyjson_val *value = yyjson_obj_getn(obj, key.data(), key.size());
  if (value == nullptr ||
      (!yyjson_is_uint(value) &&
       !(yyjson_is_int(value) && yyjson_get_int(value) >= 0))) {
    return std::unexpected(
        err("type_mismatch", "Expected non-negative integer", join(path, key)));
  }
  const std::uint64_t number = yyjson_get_uint(value);
  if (number < min_v || number > max_v) {
    return std::unexpected(
        err("out_of_range", "Integer out of range", join(path, key)));
  }
  return number;
}

std::expected<AuthoringNode, diagnostics::Error>
parse_node(yyjson_val *obj, std::string_view path) {
  AuthoringNode node;
  if (auto id = req_str(obj, "id", path); !id) {
    return std::unexpected(id.error());
  } else {
    node.id = *id;
  }
  if (auto type = req_str(obj, "type", path); !type) {
    return std::unexpected(type.error());
  } else {
    node.type = *type;
  }
  if (yyjson_val *text = yyjson_obj_get(obj, "text");
      text && yyjson_is_str(text)) {
    node.text = std::string(yyjson_get_str(text), yyjson_get_len(text));
  }
  if (yyjson_val *role = yyjson_obj_get(obj, "role");
      role && yyjson_is_str(role)) {
    node.role = std::string(yyjson_get_str(role), yyjson_get_len(role));
  }
  if (yyjson_val *visible = yyjson_obj_get(obj, "visible");
      visible && yyjson_is_bool(visible)) {
    node.visible = yyjson_get_bool(visible);
  }
  if (yyjson_val *transform = yyjson_obj_get(obj, "transform");
      transform && yyjson_is_obj(transform)) {
    if (yyjson_val *translate = yyjson_obj_get(transform, "translate");
        translate && yyjson_is_obj(translate)) {
      if (yyjson_val *x = yyjson_obj_get(translate, "x");
          x && yyjson_is_num(x)) {
        node.translate_x = yyjson_get_num(x);
      }
      if (yyjson_val *y = yyjson_obj_get(translate, "y");
          y && yyjson_is_num(y)) {
        node.translate_y = yyjson_get_num(y);
      }
    }
    if (yyjson_val *opacity = yyjson_obj_get(transform, "opacity");
        opacity && yyjson_is_num(opacity)) {
      node.opacity = yyjson_get_num(opacity);
    }
  }
  if (yyjson_val *animations = yyjson_obj_get(obj, "animations");
      animations && yyjson_is_arr(animations)) {
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(animations, &iter);
    yyjson_val *anim;
    std::size_t index = 0;
    while ((anim = yyjson_arr_iter_next(&iter)) != nullptr) {
      if (!yyjson_is_obj(anim)) {
        return std::unexpected(
            err("type_mismatch", "Expected animation object",
                join(path, "animations/" + std::to_string(index))));
      }
      const std::string anim_path =
          join(path, "animations/" + std::to_string(index));
      AuthoringAnimation animation;
      if (auto property = req_str(anim, "property", anim_path); !property) {
        return std::unexpected(property.error());
      } else {
        animation.property = *property;
      }
      yyjson_val *keyframes = yyjson_obj_get(anim, "keyframes");
      if (keyframes == nullptr || !yyjson_is_arr(keyframes) ||
          yyjson_arr_size(keyframes) == 0) {
        return std::unexpected(err("missing_field",
                                   "Animation keyframes are required",
                                   join(anim_path, "keyframes")));
      }
      yyjson_arr_iter kf_iter;
      yyjson_arr_iter_init(keyframes, &kf_iter);
      yyjson_val *kf;
      std::size_t kf_index = 0;
      while ((kf = yyjson_arr_iter_next(&kf_iter)) != nullptr) {
        if (!yyjson_is_obj(kf)) {
          return std::unexpected(
              err("type_mismatch", "Expected keyframe object",
                  join(anim_path, "keyframes/" + std::to_string(kf_index))));
        }
        const std::string kf_path =
            join(anim_path, "keyframes/" + std::to_string(kf_index));
        AuthoringKeyframe keyframe;
        if (auto frame = req_uint(kf, "frame", kf_path, 0, 1000000); !frame) {
          return std::unexpected(frame.error());
        } else {
          keyframe.frame = *frame;
        }
        yyjson_val *value = yyjson_obj_get(kf, "value");
        if (value == nullptr || !yyjson_is_num(value) ||
            !std::isfinite(yyjson_get_num(value))) {
          return std::unexpected(err("type_mismatch",
                                     "Keyframe value must be a finite number",
                                     join(kf_path, "value")));
        }
        keyframe.value = yyjson_get_num(value);
        animation.keyframes.push_back(keyframe);
        ++kf_index;
      }
      node.animations.push_back(std::move(animation));
      ++index;
    }
  }
  if (yyjson_val *children = yyjson_obj_get(obj, "children");
      children && yyjson_is_arr(children)) {
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(children, &iter);
    yyjson_val *child;
    std::size_t index = 0;
    while ((child = yyjson_arr_iter_next(&iter)) != nullptr) {
      if (!yyjson_is_obj(child)) {
        return std::unexpected(
            err("type_mismatch", "Expected node object",
                join(path, "children/" + std::to_string(index))));
      }
      auto parsed =
          parse_node(child, join(path, "children/" + std::to_string(index)));
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      node.children.push_back(std::move(*parsed));
      ++index;
    }
  }
  return node;
}

std::expected<AuthoringDocument, diagnostics::Error> decode(yyjson_val *root) {
  if (!yyjson_is_obj(root)) {
    return std::unexpected(err("type_mismatch", "Root must be an object", "/"));
  }
  AuthoringDocument doc;
  if (auto schema = req_str(root, "schema_version", ""); !schema) {
    return std::unexpected(schema.error());
  } else if (*schema != "1.0.0") {
    return std::unexpected(diagnostics::Error{
        diagnostics::ExitCode::incompatible_schema,
        diagnostics::ErrorKind::incompatible_schema, "unsupported_schema",
        "authoring", "Unsupported authoring schema_version"}
                               .with_json_path("/schema_version"));
  } else {
    doc.schema_version = *schema;
  }
  if (auto film = req_str(root, "film_id", ""); !film) {
    return std::unexpected(film.error());
  } else {
    doc.film_id = *film;
  }
  yyjson_val *canvas = yyjson_obj_get(root, "canvas");
  if (canvas == nullptr || !yyjson_is_obj(canvas)) {
    return std::unexpected(
        err("missing_field", "canvas is required", "/canvas"));
  }
  if (auto w = req_uint(canvas, "width", "/canvas", 1, 16384); !w) {
    return std::unexpected(w.error());
  } else {
    doc.canvas_width = static_cast<std::uint32_t>(*w);
  }
  if (auto h = req_uint(canvas, "height", "/canvas", 1, 16384); !h) {
    return std::unexpected(h.error());
  } else {
    doc.canvas_height = static_cast<std::uint32_t>(*h);
  }
  if (auto color = req_str(canvas, "color", "/canvas"); !color) {
    return std::unexpected(color.error());
  } else {
    doc.canvas_color = *color;
  }

  yyjson_val *rate = yyjson_obj_get(root, "frame_rate");
  if (rate == nullptr || !yyjson_is_obj(rate)) {
    return std::unexpected(
        err("missing_field", "frame_rate is required", "/frame_rate"));
  }
  auto num = req_uint(rate, "numerator", "/frame_rate", 1, 240000);
  auto den = req_uint(rate, "denominator", "/frame_rate", 1, 1001);
  if (!num) {
    return std::unexpected(num.error());
  }
  if (!den) {
    return std::unexpected(den.error());
  }
  if (auto made = core::FrameRate::make(static_cast<std::uint32_t>(*num),
                                        static_cast<std::uint32_t>(*den));
      !made) {
    return std::unexpected(
        std::move(made.error()).with_json_path("/frame_rate"));
  } else {
    doc.frame_rate = *made;
  }

  yyjson_val *seed = yyjson_obj_get(root, "seed");
  if (seed == nullptr) {
    return std::unexpected(err("missing_field", "seed is required", "/seed"));
  }
  if (yyjson_is_uint(seed) ||
      (yyjson_is_int(seed) && yyjson_get_int(seed) >= 0)) {
    auto parsed = core::Uint64Encoding::from_json_number(yyjson_get_uint(seed));
    if (!parsed) {
      return std::unexpected(std::move(parsed.error()).with_json_path("/seed"));
    }
    doc.seed = *parsed;
  } else if (yyjson_is_str(seed)) {
    auto parsed = core::Uint64Encoding::from_json_string(
        std::string_view(yyjson_get_str(seed), yyjson_get_len(seed)));
    if (!parsed) {
      return std::unexpected(std::move(parsed.error()).with_json_path("/seed"));
    }
    doc.seed = *parsed;
  } else {
    return std::unexpected(err("type_mismatch", "Invalid seed", "/seed"));
  }

  if (auto style = req_str(root, "style", ""); !style) {
    return std::unexpected(style.error());
  } else {
    doc.style = *style;
  }

  if (yyjson_val *transitions = yyjson_obj_get(root, "transitions");
      transitions != nullptr) {
    if (!yyjson_is_arr(transitions)) {
      return std::unexpected(
          err("type_mismatch", "transitions must be an array", "/transitions"));
    }
    yyjson_arr_iter tr_iter;
    yyjson_arr_iter_init(transitions, &tr_iter);
    yyjson_val *tr_val;
    std::size_t tr_index = 0;
    while ((tr_val = yyjson_arr_iter_next(&tr_iter)) != nullptr) {
      const std::string tpath = "/transitions/" + std::to_string(tr_index++);
      if (!yyjson_is_obj(tr_val)) {
        return std::unexpected(err("type_mismatch", "Expected object", tpath));
      }
      AuthoringTransition transition;
      if (auto id = req_str(tr_val, "id", tpath); !id) {
        return std::unexpected(id.error());
      } else {
        transition.id = *id;
      }
      if (auto from = req_str(tr_val, "from_shot_id", tpath); !from) {
        return std::unexpected(from.error());
      } else {
        transition.from_shot_id = *from;
      }
      if (auto to = req_str(tr_val, "to_shot_id", tpath); !to) {
        return std::unexpected(to.error());
      } else {
        transition.to_shot_id = *to;
      }
      if (auto overlap =
              req_uint(tr_val, "overlap_frames", tpath, 0, 1000000ULL);
          !overlap) {
        return std::unexpected(overlap.error());
      } else {
        transition.overlap_frames = *overlap;
      }
      if (auto blend = req_str(tr_val, "blend", tpath); !blend) {
        return std::unexpected(blend.error());
      } else {
        transition.blend = *blend;
      }
      doc.transitions.push_back(std::move(transition));
    }
  }

  yyjson_val *shots = yyjson_obj_get(root, "shots");
  if (shots == nullptr || !yyjson_is_arr(shots) || yyjson_arr_size(shots) < 1) {
    return std::unexpected(
        err("missing_field", "shots must be a non-empty array", "/shots"));
  }
  yyjson_arr_iter shot_iter;
  yyjson_arr_iter_init(shots, &shot_iter);
  yyjson_val *shot_val;
  std::size_t shot_index = 0;
  while ((shot_val = yyjson_arr_iter_next(&shot_iter)) != nullptr) {
    const std::string spath = "/shots/" + std::to_string(shot_index++);
    if (!yyjson_is_obj(shot_val)) {
      return std::unexpected(err("type_mismatch", "Expected object", spath));
    }
    AuthoringShot shot;
    if (auto id = req_str(shot_val, "id", spath); !id) {
      return std::unexpected(id.error());
    } else {
      shot.id = *id;
    }
    if (auto duration =
            req_uint(shot_val, "duration_frames", spath, 1, 1000000);
        !duration) {
      return std::unexpected(duration.error());
    } else {
      shot.duration_frames = *duration;
    }
    if (yyjson_val *style_override = yyjson_obj_get(shot_val, "style_override");
        style_override && yyjson_is_str(style_override)) {
      shot.style_override = std::string(yyjson_get_str(style_override),
                                        yyjson_get_len(style_override));
    }
    if (yyjson_val *intent = yyjson_obj_get(shot_val, "intent");
        intent && yyjson_is_obj(intent)) {
      AuthoringIntent value;
      if (auto type = req_str(intent, "type", join(spath, "intent")); !type) {
        return std::unexpected(type.error());
      } else {
        value.type = *type;
      }
      yyjson_val *intensity = yyjson_obj_get(intent, "intensity");
      if (intensity == nullptr || !yyjson_is_num(intensity) ||
          !std::isfinite(yyjson_get_num(intensity))) {
        return std::unexpected(err("type_mismatch", "intensity required",
                                   join(spath, "intent/intensity")));
      }
      value.intensity = yyjson_get_num(intensity);
      if (yyjson_val *importance = yyjson_obj_get(intent, "importance");
          importance && yyjson_is_num(importance)) {
        value.importance = yyjson_get_num(importance);
      }
      if (yyjson_val *targets = yyjson_obj_get(intent, "target_ids");
          targets && yyjson_is_arr(targets)) {
        yyjson_arr_iter t_iter;
        yyjson_arr_iter_init(targets, &t_iter);
        yyjson_val *t_val;
        while ((t_val = yyjson_arr_iter_next(&t_iter)) != nullptr) {
          if (!yyjson_is_str(t_val)) {
            return std::unexpected(err("type_mismatch", "target id string",
                                       join(spath, "intent/target_ids")));
          }
          value.target_ids.emplace_back(yyjson_get_str(t_val));
        }
      }
      shot.intent = std::move(value);
    }
    yyjson_val *nodes = yyjson_obj_get(shot_val, "nodes");
    if (nodes == nullptr || !yyjson_is_arr(nodes)) {
      return std::unexpected(
          err("missing_field", "nodes required", join(spath, "nodes")));
    }
    yyjson_arr_iter node_iter;
    yyjson_arr_iter_init(nodes, &node_iter);
    yyjson_val *node_val;
    std::size_t node_index = 0;
    while ((node_val = yyjson_arr_iter_next(&node_iter)) != nullptr) {
      if (!yyjson_is_obj(node_val)) {
        return std::unexpected(
            err("type_mismatch", "Expected node",
                join(spath, "nodes/" + std::to_string(node_index))));
      }
      auto parsed = parse_node(
          node_val, join(spath, "nodes/" + std::to_string(node_index)));
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      shot.nodes.push_back(std::move(*parsed));
      ++node_index;
    }
    doc.shots.push_back(std::move(shot));
  }
  return doc;
}

} // namespace

std::expected<AuthoringDocument, diagnostics::Error>
load_authoring_json(std::span<const std::byte> bytes,
                    const LoadOptions &options) {
  if (bytes.size() > options.limits.max_bytes) {
    return std::unexpected(
        err("limit_exceeded", "JSON input exceeds byte limit", "/"));
  }
  if (bytes.empty()) {
    return std::unexpected(err("invalid_json", "JSON input is empty", "/"));
  }
  auto migrated = migrate_authoring_json(bytes);
  if (!migrated) {
    return std::unexpected(migrated.error());
  }
  // Decode owns a mutable parse buffer; migration already copied input.
  std::string buffer = std::move(*migrated);
  yyjson_read_err read_err{};
  DocGuard guard{
      yyjson_read_opts(buffer.data(), buffer.size(), 0, nullptr, &read_err)};
  if (guard.doc == nullptr) {
    return std::unexpected(
        err("invalid_json",
            read_err.msg != nullptr ? read_err.msg : "JSON parse failed", "/"));
  }
  return decode(yyjson_doc_get_root(guard.doc));
}

std::expected<AuthoringDocument, diagnostics::Error>
load_authoring_json(std::string_view text, const LoadOptions &options) {
  const auto *data = reinterpret_cast<const std::byte *>(text.data());
  return load_authoring_json(std::span<const std::byte>(data, text.size()),
                             options);
}

} // namespace instantee::scene_ir
