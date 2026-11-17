#include <instantee/scene_ir/load.hpp>

#include <instantee/core_model/integer_encoding.hpp>
#include <instantee/scene_ir/uri_sandbox.hpp>

#include <yyjson.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <utility>
#include <vector>

namespace instantee::scene_ir {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message,
                       std::string json_path) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "scene_ir", message}
      .with_json_path(std::move(json_path));
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
      return std::unexpected(err("unknown_field", "Unknown object field",
                                 path_join(json_path, name)));
    }
  }
  return {};
}

std::expected<yyjson_val *, diagnostics::Error>
require_obj(yyjson_val *parent, std::string_view key, std::string_view path) {
  yyjson_val *value = yyjson_obj_getn(parent, key.data(), key.size());
  if (value == nullptr) {
    return std::unexpected(err("missing_field", "Required field is missing",
                               path_join(path, key)));
  }
  if (!yyjson_is_obj(value)) {
    return std::unexpected(
        err("type_mismatch", "Expected object", path_join(path, key)));
  }
  return value;
}

std::expected<yyjson_val *, diagnostics::Error>
require_arr(yyjson_val *parent, std::string_view key, std::string_view path) {
  yyjson_val *value = yyjson_obj_getn(parent, key.data(), key.size());
  if (value == nullptr) {
    return std::unexpected(err("missing_field", "Required field is missing",
                               path_join(path, key)));
  }
  if (!yyjson_is_arr(value)) {
    return std::unexpected(
        err("type_mismatch", "Expected array", path_join(path, key)));
  }
  return value;
}

std::expected<std::string, diagnostics::Error>
require_string(yyjson_val *parent, std::string_view key, std::string_view path,
               std::size_t max_bytes) {
  yyjson_val *value = yyjson_obj_getn(parent, key.data(), key.size());
  if (value == nullptr) {
    return std::unexpected(err("missing_field", "Required field is missing",
                               path_join(path, key)));
  }
  if (!yyjson_is_str(value)) {
    return std::unexpected(
        err("type_mismatch", "Expected string", path_join(path, key)));
  }
  const char *text = yyjson_get_str(value);
  const std::size_t len = yyjson_get_len(value);
  if (text == nullptr || len > max_bytes) {
    return std::unexpected(
        err("string_too_long", "String exceeds limit", path_join(path, key)));
  }
  return std::string(text, len);
}

std::expected<std::uint64_t, diagnostics::Error>
require_uint(yyjson_val *parent, std::string_view key, std::string_view path,
             std::uint64_t min_inclusive, std::uint64_t max_inclusive) {
  yyjson_val *value = yyjson_obj_getn(parent, key.data(), key.size());
  if (value == nullptr) {
    return std::unexpected(err("missing_field", "Required field is missing",
                               path_join(path, key)));
  }
  if (!yyjson_is_uint(value) &&
      !(yyjson_is_int(value) && yyjson_get_int(value) >= 0)) {
    return std::unexpected(err("type_mismatch", "Expected non-negative integer",
                               path_join(path, key)));
  }
  const std::uint64_t number = yyjson_get_uint(value);
  if (number < min_inclusive || number > max_inclusive) {
    return std::unexpected(err("out_of_range", "Integer outside allowed range",
                               path_join(path, key)));
  }
  return number;
}

std::expected<double, diagnostics::Error>
require_number(yyjson_val *parent, std::string_view key,
               std::string_view path) {
  yyjson_val *value = yyjson_obj_getn(parent, key.data(), key.size());
  if (value == nullptr) {
    return std::unexpected(err("missing_field", "Required field is missing",
                               path_join(path, key)));
  }
  if (!yyjson_is_num(value)) {
    return std::unexpected(
        err("type_mismatch", "Expected number", path_join(path, key)));
  }
  const double number = yyjson_get_num(value);
  if (!std::isfinite(number)) {
    return std::unexpected(err("non_finite_number", "NaN/Infinity are rejected",
                               path_join(path, key)));
  }
  return number;
}

std::expected<std::uint64_t, diagnostics::Error>
parse_seed(yyjson_val *root, std::string_view path) {
  yyjson_val *value = yyjson_obj_get(root, "seed");
  if (value == nullptr) {
    return std::unexpected(err("missing_field", "Required field is missing",
                               path_join(path, "seed")));
  }
  if (yyjson_is_uint(value) ||
      (yyjson_is_int(value) && yyjson_get_int(value) >= 0)) {
    return core::Uint64Encoding::from_json_number(yyjson_get_uint(value))
        .transform_error([&](diagnostics::Error e) {
          return std::move(e).with_json_path(path_join(path, "seed"));
        });
  }
  if (yyjson_is_str(value)) {
    const char *text = yyjson_get_str(value);
    const std::size_t len = yyjson_get_len(value);
    if (text == nullptr) {
      return std::unexpected(err("type_mismatch", "Expected seed string",
                                 path_join(path, "seed")));
    }
    return core::Uint64Encoding::from_json_string(std::string_view(text, len))
        .transform_error([&](diagnostics::Error e) {
          return std::move(e).with_json_path(path_join(path, "seed"));
        });
  }
  return std::unexpected(err("type_mismatch",
                             "Seed must be a safe integer or decimal string",
                             path_join(path, "seed")));
}

std::expected<Vec3, diagnostics::Error> parse_vec3(yyjson_val *obj,
                                                   std::string_view path) {
  static constexpr std::string_view kKeys[] = {"x", "y", "z"};
  if (auto check = reject_unknown_keys(obj, path, kKeys); !check) {
    return std::unexpected(check.error());
  }
  Vec3 out;
  if (auto x = require_number(obj, "x", path); !x) {
    return std::unexpected(x.error());
  } else {
    out.x = *x;
  }
  if (auto y = require_number(obj, "y", path); !y) {
    return std::unexpected(y.error());
  } else {
    out.y = *y;
  }
  if (auto z = require_number(obj, "z", path); !z) {
    return std::unexpected(z.error());
  } else {
    out.z = *z;
  }
  return out;
}

std::expected<Transform, diagnostics::Error>
parse_transform(yyjson_val *obj, std::string_view path) {
  static constexpr std::string_view kKeys[] = {
      "translate", "scale", "rotation_z_rad", "opacity", "pivot"};
  if (auto check = reject_unknown_keys(obj, path, kKeys); !check) {
    return std::unexpected(check.error());
  }
  Transform out;
  if (yyjson_val *translate = yyjson_obj_get(obj, "translate")) {
    if (!yyjson_is_obj(translate)) {
      return std::unexpected(err("type_mismatch", "Expected object",
                                 path_join(path, "translate")));
    }
    if (auto parsed = parse_vec3(translate, path_join(path, "translate"));
        !parsed) {
      return std::unexpected(parsed.error());
    } else {
      out.translate = *parsed;
    }
  }
  if (yyjson_val *scale = yyjson_obj_get(obj, "scale")) {
    if (!yyjson_is_obj(scale)) {
      return std::unexpected(
          err("type_mismatch", "Expected object", path_join(path, "scale")));
    }
    if (auto parsed = parse_vec3(scale, path_join(path, "scale")); !parsed) {
      return std::unexpected(parsed.error());
    } else {
      out.scale = *parsed;
    }
  }
  if (yyjson_val *rotation = yyjson_obj_get(obj, "rotation_z_rad")) {
    if (!yyjson_is_num(rotation) || !std::isfinite(yyjson_get_num(rotation))) {
      return std::unexpected(err("type_mismatch", "Expected finite number",
                                 path_join(path, "rotation_z_rad")));
    }
    out.rotation_z_rad = yyjson_get_num(rotation);
  }
  if (yyjson_val *opacity = yyjson_obj_get(obj, "opacity")) {
    if (!yyjson_is_num(opacity) || !std::isfinite(yyjson_get_num(opacity))) {
      return std::unexpected(err("type_mismatch", "Expected finite number",
                                 path_join(path, "opacity")));
    }
    const double value = yyjson_get_num(opacity);
    if (value < 0.0 || value > 1.0) {
      return std::unexpected(err("out_of_range", "Opacity must be in [0,1]",
                                 path_join(path, "opacity")));
    }
    out.opacity = value;
  }
  if (yyjson_val *pivot = yyjson_obj_get(obj, "pivot")) {
    if (!yyjson_is_obj(pivot)) {
      return std::unexpected(
          err("type_mismatch", "Expected object", path_join(path, "pivot")));
    }
    if (auto parsed = parse_vec3(pivot, path_join(path, "pivot")); !parsed) {
      return std::unexpected(parsed.error());
    } else {
      out.pivot = *parsed;
    }
  }
  return out;
}

std::expected<NodeKind, diagnostics::Error>
parse_node_kind(std::string_view text, std::string_view path) {
  if (text == "group") {
    return NodeKind::group;
  }
  if (text == "geometry") {
    return NodeKind::geometry;
  }
  if (text == "text") {
    return NodeKind::text;
  }
  if (text == "image") {
    return NodeKind::image;
  }
  if (text == "puppet") {
    return NodeKind::puppet;
  }
  if (text == "camera") {
    return NodeKind::camera;
  }
  return std::unexpected(
      err("unknown_enum", "Unknown node kind", std::string(path)));
}

struct DocGuard {
  yyjson_doc *doc = nullptr;
  ~DocGuard() { yyjson_doc_free(doc); }
  DocGuard(const DocGuard &) = delete;
  DocGuard &operator=(const DocGuard &) = delete;
  DocGuard(DocGuard &&other) noexcept : doc(other.doc) { other.doc = nullptr; }
  DocGuard &operator=(DocGuard &&other) noexcept {
    if (this != &other) {
      yyjson_doc_free(doc);
      doc = other.doc;
      other.doc = nullptr;
    }
    return *this;
  }
  explicit DocGuard(yyjson_doc *owned) : doc(owned) {}
};

std::expected<SceneDocument, diagnostics::Error>
decode_scene(yyjson_val *root, const LoadOptions &options) {
  if (!yyjson_is_obj(root)) {
    return std::unexpected(err("type_mismatch", "Root must be an object", "/"));
  }

  static constexpr std::string_view kRootKeys[] = {
      "schema_version",  "compiler_version",
      "canvas",          "frame_rate",
      "duration_frames", "seed",
      "assets",          "materials",
      "geometry",        "text_runs",
      "cameras",         "rigs",
      "nodes",           "animation_tracks",
      "shots",           "transition_spans",
      "provenance"};
  if (auto check = reject_unknown_keys(root, "", kRootKeys); !check) {
    return std::unexpected(check.error());
  }

  SceneDocument doc;
  if (auto schema = require_string(root, "schema_version", "", 32); !schema) {
    return std::unexpected(schema.error());
  } else if (*schema != "1.0.0") {
    return std::unexpected(err(
        "unsupported_schema", "Unsupported schema_version", "/schema_version"));
  } else {
    doc.schema_version = *schema;
  }

  if (auto compiler = require_string(root, "compiler_version", "", 64);
      !compiler) {
    return std::unexpected(compiler.error());
  } else {
    doc.compiler_version = *compiler;
  }

  auto canvas = require_obj(root, "canvas", "");
  if (!canvas) {
    return std::unexpected(canvas.error());
  }
  static constexpr std::string_view kCanvasKeys[] = {"width", "height",
                                                     "color"};
  if (auto check = reject_unknown_keys(*canvas, "/canvas", kCanvasKeys);
      !check) {
    return std::unexpected(check.error());
  }
  if (auto width = require_uint(*canvas, "width", "/canvas", 1, 16384);
      !width) {
    return std::unexpected(width.error());
  } else {
    doc.canvas_width = static_cast<std::uint32_t>(*width);
  }
  if (auto height = require_uint(*canvas, "height", "/canvas", 1, 16384);
      !height) {
    return std::unexpected(height.error());
  } else {
    doc.canvas_height = static_cast<std::uint32_t>(*height);
  }
  if (auto color = require_string(*canvas, "color", "/canvas", 32); !color) {
    return std::unexpected(color.error());
  } else if (*color != "rec709-sdr") {
    return std::unexpected(
        err("unknown_enum", "Unsupported canvas color", "/canvas/color"));
  } else {
    doc.canvas_color = *color;
  }

  auto rate = require_obj(root, "frame_rate", "");
  if (!rate) {
    return std::unexpected(rate.error());
  }
  static constexpr std::string_view kRateKeys[] = {"numerator", "denominator"};
  if (auto check = reject_unknown_keys(*rate, "/frame_rate", kRateKeys);
      !check) {
    return std::unexpected(check.error());
  }
  auto numerator = require_uint(*rate, "numerator", "/frame_rate", 1, 240000);
  auto denominator = require_uint(*rate, "denominator", "/frame_rate", 1, 1001);
  if (!numerator) {
    return std::unexpected(numerator.error());
  }
  if (!denominator) {
    return std::unexpected(denominator.error());
  }
  if (auto made =
          core::FrameRate::make(static_cast<std::uint32_t>(*numerator),
                                static_cast<std::uint32_t>(*denominator));
      !made) {
    return std::unexpected(
        std::move(made.error()).with_json_path("/frame_rate"));
  } else {
    doc.frame_rate = *made;
  }

  if (auto duration =
          require_uint(root, "duration_frames", "", 1, 1000000000ULL);
      !duration) {
    return std::unexpected(duration.error());
  } else {
    doc.duration_frames = core::FrameIndex{*duration};
  }

  if (auto seed = parse_seed(root, ""); !seed) {
    return std::unexpected(seed.error());
  } else {
    doc.seed = *seed;
  }

  auto assets = require_arr(root, "assets", "");
  if (!assets) {
    return std::unexpected(assets.error());
  }
  if (yyjson_arr_size(*assets) > options.limits.max_array_items) {
    return std::unexpected(
        err("limit_exceeded", "Array exceeds item limit", "/assets"));
  }
  yyjson_arr_iter asset_iter;
  yyjson_arr_iter_init(*assets, &asset_iter);
  yyjson_val *asset_val;
  std::size_t asset_index = 0;
  while ((asset_val = yyjson_arr_iter_next(&asset_iter)) != nullptr) {
    const std::string apath = path_index("/assets", asset_index++);
    if (!yyjson_is_obj(asset_val)) {
      return std::unexpected(err("type_mismatch", "Expected object", apath));
    }
    static constexpr std::string_view kAssetKeys[] = {
        "id", "uri", "sha256", "media_type", "color_profile"};
    if (auto check = reject_unknown_keys(asset_val, apath, kAssetKeys);
        !check) {
      return std::unexpected(check.error());
    }
    Asset asset;
    if (auto id = require_string(asset_val, "id", apath, 128); !id) {
      return std::unexpected(id.error());
    } else {
      asset.id = *id;
    }
    if (auto uri = require_string(asset_val, "uri", apath, 512); !uri) {
      return std::unexpected(uri.error());
    } else {
      asset.uri = *uri;
    }
    if (!is_safe_asset_uri_syntax(asset.uri)) {
      return std::unexpected(err("unsafe_asset_uri",
                                 "Asset URI failed sandbox syntax",
                                 path_join(apath, "uri")));
    }
    if (options.enforce_uri_sandbox) {
      if (auto resolved = resolve_asset_uri(options.project_root, asset.uri);
          !resolved) {
        return std::unexpected(std::move(resolved.error())
                                   .with_json_path(path_join(apath, "uri")));
      }
    }
    if (auto sha = require_string(asset_val, "sha256", apath, 64); !sha) {
      return std::unexpected(sha.error());
    } else if (sha->size() != 64) {
      return std::unexpected(err("type_mismatch", "sha256 must be 64 hex chars",
                                 path_join(apath, "sha256")));
    } else {
      asset.sha256 = *sha;
    }
    if (auto media = require_string(asset_val, "media_type", apath, 100);
        !media) {
      return std::unexpected(media.error());
    } else {
      asset.media_type = *media;
    }
    if (yyjson_val *profile = yyjson_obj_get(asset_val, "color_profile")) {
      if (!yyjson_is_str(profile)) {
        return std::unexpected(err("type_mismatch", "Expected string",
                                   path_join(apath, "color_profile")));
      }
      asset.color_profile = yyjson_get_str(profile);
    }
    doc.assets.push_back(std::move(asset));
  }

  auto materials = require_arr(root, "materials", "");
  if (!materials) {
    return std::unexpected(materials.error());
  }
  yyjson_arr_iter mat_iter;
  yyjson_arr_iter_init(*materials, &mat_iter);
  yyjson_val *mat_val;
  std::size_t mat_index = 0;
  while ((mat_val = yyjson_arr_iter_next(&mat_iter)) != nullptr) {
    const std::string mpath = path_index("/materials", mat_index++);
    if (!yyjson_is_obj(mat_val)) {
      return std::unexpected(err("type_mismatch", "Expected object", mpath));
    }
    static constexpr std::string_view kMatKeys[] = {"id", "shader_feature",
                                                    "opacity", "parameters"};
    if (auto check = reject_unknown_keys(mat_val, mpath, kMatKeys); !check) {
      return std::unexpected(check.error());
    }
    Material material;
    if (auto id = require_string(mat_val, "id", mpath, 128); !id) {
      return std::unexpected(id.error());
    } else {
      material.id = *id;
    }
    if (auto feature = require_string(mat_val, "shader_feature", mpath, 128);
        !feature) {
      return std::unexpected(feature.error());
    } else {
      material.shader_feature = *feature;
    }
    if (auto opacity = require_number(mat_val, "opacity", mpath); !opacity) {
      return std::unexpected(opacity.error());
    } else if (*opacity < 0.0 || *opacity > 1.0) {
      return std::unexpected(err("out_of_range", "Opacity must be in [0,1]",
                                 path_join(mpath, "opacity")));
    } else {
      material.opacity = *opacity;
    }
    doc.materials.push_back(std::move(material));
  }

  auto geometry = require_arr(root, "geometry", "");
  if (!geometry) {
    return std::unexpected(geometry.error());
  }
  yyjson_arr_iter geom_iter;
  yyjson_arr_iter_init(*geometry, &geom_iter);
  yyjson_val *geom_val;
  std::size_t geom_index = 0;
  while ((geom_val = yyjson_arr_iter_next(&geom_iter)) != nullptr) {
    const std::string gpath = path_index("/geometry", geom_index++);
    if (!yyjson_is_obj(geom_val)) {
      return std::unexpected(err("type_mismatch", "Expected object", gpath));
    }
    if (auto id = require_string(geom_val, "id", gpath, 128); !id) {
      return std::unexpected(id.error());
    } else {
      doc.geometry_ids.push_back(*id);
    }
  }

  auto text_runs = require_arr(root, "text_runs", "");
  if (!text_runs) {
    return std::unexpected(text_runs.error());
  }
  yyjson_arr_iter text_iter;
  yyjson_arr_iter_init(*text_runs, &text_iter);
  yyjson_val *text_val;
  std::size_t text_index = 0;
  while ((text_val = yyjson_arr_iter_next(&text_iter)) != nullptr) {
    const std::string tpath = path_index("/text_runs", text_index++);
    if (!yyjson_is_obj(text_val)) {
      return std::unexpected(err("type_mismatch", "Expected object", tpath));
    }
    static constexpr std::string_view kTextKeys[] = {
        "id",           "text",         "font_asset_id",
        "font_size_px", "max_width_px", "text_role"};
    if (auto check = reject_unknown_keys(text_val, tpath, kTextKeys); !check) {
      return std::unexpected(check.error());
    }
    TextRun run;
    if (auto id = require_string(text_val, "id", tpath, 128); !id) {
      return std::unexpected(id.error());
    } else {
      run.id = *id;
    }
    if (auto text = require_string(text_val, "text", tpath,
                                   options.limits.max_string_bytes);
        !text) {
      return std::unexpected(text.error());
    } else {
      run.text = *text;
    }
    if (auto font = require_string(text_val, "font_asset_id", tpath, 128);
        !font) {
      return std::unexpected(font.error());
    } else {
      run.font_asset_id = *font;
    }
    if (auto size = require_number(text_val, "font_size_px", tpath); !size) {
      return std::unexpected(size.error());
    } else if (*size <= 0.0) {
      return std::unexpected(err("out_of_range", "font_size_px must be > 0",
                                 path_join(tpath, "font_size_px")));
    } else {
      run.font_size_px = *size;
    }
    if (yyjson_val *max_width = yyjson_obj_get(text_val, "max_width_px")) {
      if (!yyjson_is_num(max_width)) {
        return std::unexpected(err("type_mismatch", "Expected number",
                                   path_join(tpath, "max_width_px")));
      }
      const double width = yyjson_get_num(max_width);
      if (!(width > 0.0)) {
        return std::unexpected(err("out_of_range", "max_width_px must be > 0",
                                   path_join(tpath, "max_width_px")));
      }
      run.max_width_px = width;
    }
    if (yyjson_val *role = yyjson_obj_get(text_val, "text_role")) {
      if (!yyjson_is_str(role)) {
        return std::unexpected(err("type_mismatch", "Expected string",
                                   path_join(tpath, "text_role")));
      }
      const char *role_text = yyjson_get_str(role);
      const auto role_len = yyjson_get_len(role);
      if (role_text == nullptr || role_len == 0 || role_len > 128) {
        return std::unexpected(err("invalid_string", "text_role is invalid",
                                   path_join(tpath, "text_role")));
      }
      run.text_role = std::string(role_text, role_len);
    }
    doc.text_runs.push_back(std::move(run));
  }

  if (yyjson_val *cameras = yyjson_obj_get(root, "cameras")) {
    if (!yyjson_is_arr(cameras)) {
      return std::unexpected(
          err("type_mismatch", "Expected array", "/cameras"));
    }
    yyjson_arr_iter cam_iter;
    yyjson_arr_iter_init(cameras, &cam_iter);
    yyjson_val *cam_val;
    std::size_t cam_index = 0;
    while ((cam_val = yyjson_arr_iter_next(&cam_iter)) != nullptr) {
      const std::string cpath = path_index("/cameras", cam_index++);
      if (!yyjson_is_obj(cam_val)) {
        return std::unexpected(err("type_mismatch", "Expected object", cpath));
      }
      static constexpr std::string_view kCamKeys[] = {
          "id", "projection", "near", "far", "fov_y_rad", "transform"};
      if (auto check = reject_unknown_keys(cam_val, cpath, kCamKeys); !check) {
        return std::unexpected(check.error());
      }
      Camera camera;
      if (auto id = require_string(cam_val, "id", cpath, 128); !id) {
        return std::unexpected(id.error());
      } else {
        camera.id = *id;
      }
      if (auto projection = require_string(cam_val, "projection", cpath, 32);
          !projection) {
        return std::unexpected(projection.error());
      } else if (*projection != "orthographic" &&
                 *projection != "perspective") {
        return std::unexpected(err("unknown_enum", "Unknown camera projection",
                                   path_join(cpath, "projection")));
      } else {
        camera.projection = *projection;
      }
      if (yyjson_val *near_v = yyjson_obj_get(cam_val, "near")) {
        if (!yyjson_is_num(near_v) || yyjson_get_num(near_v) <= 0.0) {
          return std::unexpected(err("out_of_range", "near must be > 0",
                                     path_join(cpath, "near")));
        }
        camera.near = yyjson_get_num(near_v);
      }
      if (yyjson_val *far_v = yyjson_obj_get(cam_val, "far")) {
        if (!yyjson_is_num(far_v) || yyjson_get_num(far_v) <= 0.0) {
          return std::unexpected(
              err("out_of_range", "far must be > 0", path_join(cpath, "far")));
        }
        camera.far = yyjson_get_num(far_v);
      }
      doc.cameras.push_back(std::move(camera));
    }
  }

  if (yyjson_val *rigs = yyjson_obj_get(root, "rigs")) {
    if (!yyjson_is_arr(rigs)) {
      return std::unexpected(err("type_mismatch", "Expected array", "/rigs"));
    }
    yyjson_arr_iter rig_iter;
    yyjson_arr_iter_init(rigs, &rig_iter);
    yyjson_val *rig_val;
    std::size_t rig_index = 0;
    while ((rig_val = yyjson_arr_iter_next(&rig_iter)) != nullptr) {
      const std::string rpath = path_index("/rigs", rig_index++);
      if (!yyjson_is_obj(rig_val)) {
        return std::unexpected(err("type_mismatch", "Expected object", rpath));
      }
      static constexpr std::string_view kRigKeys[] = {"id", "format_version",
                                                      "payload_ref"};
      if (auto check = reject_unknown_keys(rig_val, rpath, kRigKeys); !check) {
        return std::unexpected(check.error());
      }
      Rig rig;
      if (auto id = require_string(rig_val, "id", rpath, 128); !id) {
        return std::unexpected(id.error());
      } else {
        rig.id = *id;
      }
      if (auto version = require_string(rig_val, "format_version", rpath, 32);
          !version) {
        return std::unexpected(version.error());
      } else {
        rig.format_version = *version;
      }
      if (auto payload = require_string(rig_val, "payload_ref", rpath, 512);
          !payload) {
        return std::unexpected(payload.error());
      } else {
        rig.payload_ref = *payload;
      }
      doc.rigs.push_back(std::move(rig));
    }
  }

  auto nodes = require_arr(root, "nodes", "");
  if (!nodes) {
    return std::unexpected(nodes.error());
  }
  yyjson_arr_iter node_iter;
  yyjson_arr_iter_init(*nodes, &node_iter);
  yyjson_val *node_val;
  std::size_t node_index = 0;
  while ((node_val = yyjson_arr_iter_next(&node_iter)) != nullptr) {
    const std::string npath = path_index("/nodes", node_index++);
    if (!yyjson_is_obj(node_val)) {
      return std::unexpected(err("type_mismatch", "Expected object", npath));
    }
    static constexpr std::string_view kNodeKeys[] = {
        "id",          "kind",        "parent_id",   "layer_order",
        "transform",   "geometry_id", "text_run_id", "asset_id",
        "material_id", "camera_id",   "rig_id",      "visible"};
    if (auto check = reject_unknown_keys(node_val, npath, kNodeKeys); !check) {
      return std::unexpected(check.error());
    }
    Node node;
    if (auto id = require_string(node_val, "id", npath, 128); !id) {
      return std::unexpected(id.error());
    } else {
      node.id = *id;
    }
    if (auto kind = require_string(node_val, "kind", npath, 32); !kind) {
      return std::unexpected(kind.error());
    } else if (auto parsed = parse_node_kind(*kind, path_join(npath, "kind"));
               !parsed) {
      return std::unexpected(parsed.error());
    } else {
      node.kind = *parsed;
    }
    yyjson_val *layer_raw = yyjson_obj_get(node_val, "layer_order");
    if (layer_raw == nullptr) {
      return std::unexpected(err("missing_field", "Required field is missing",
                                 path_join(npath, "layer_order")));
    }
    if (!yyjson_is_int(layer_raw) && !yyjson_is_uint(layer_raw)) {
      return std::unexpected(err("type_mismatch", "Expected integer",
                                 path_join(npath, "layer_order")));
    }
    const std::int64_t signed_layer =
        yyjson_is_uint(layer_raw)
            ? static_cast<std::int64_t>(yyjson_get_uint(layer_raw))
            : yyjson_get_sint(layer_raw);
    if (signed_layer < -1000000 || signed_layer > 1000000) {
      return std::unexpected(err("out_of_range", "layer_order out of range",
                                 path_join(npath, "layer_order")));
    }
    node.layer_order = static_cast<std::int32_t>(signed_layer);
    if (yyjson_val *parent = yyjson_obj_get(node_val, "parent_id")) {
      if (!yyjson_is_str(parent)) {
        return std::unexpected(err("type_mismatch", "Expected string",
                                   path_join(npath, "parent_id")));
      }
      node.parent_id = yyjson_get_str(parent);
    }
    if (yyjson_val *transform = yyjson_obj_get(node_val, "transform")) {
      if (!yyjson_is_obj(transform)) {
        return std::unexpected(err("type_mismatch", "Expected object",
                                   path_join(npath, "transform")));
      }
      if (auto parsed =
              parse_transform(transform, path_join(npath, "transform"));
          !parsed) {
        return std::unexpected(parsed.error());
      } else {
        node.transform = *parsed;
      }
    }
    auto optional_id = [&](const char *key, std::optional<std::string> &slot)
        -> std::expected<void, diagnostics::Error> {
      yyjson_val *value = yyjson_obj_get(node_val, key);
      if (value == nullptr) {
        return {};
      }
      if (!yyjson_is_str(value)) {
        return std::unexpected(
            err("type_mismatch", "Expected string", path_join(npath, key)));
      }
      slot = yyjson_get_str(value);
      return {};
    };
    if (auto ok = optional_id("geometry_id", node.geometry_id); !ok) {
      return std::unexpected(ok.error());
    }
    if (auto ok = optional_id("text_run_id", node.text_run_id); !ok) {
      return std::unexpected(ok.error());
    }
    if (auto ok = optional_id("asset_id", node.asset_id); !ok) {
      return std::unexpected(ok.error());
    }
    if (auto ok = optional_id("material_id", node.material_id); !ok) {
      return std::unexpected(ok.error());
    }
    if (auto ok = optional_id("camera_id", node.camera_id); !ok) {
      return std::unexpected(ok.error());
    }
    if (auto ok = optional_id("rig_id", node.rig_id); !ok) {
      return std::unexpected(ok.error());
    }
    if (yyjson_val *visible = yyjson_obj_get(node_val, "visible")) {
      if (!yyjson_is_bool(visible)) {
        return std::unexpected(err("type_mismatch", "Expected boolean",
                                   path_join(npath, "visible")));
      }
      node.visible = yyjson_get_bool(visible);
    }
    doc.nodes.push_back(std::move(node));
  }

  auto tracks = require_arr(root, "animation_tracks", "");
  if (!tracks) {
    return std::unexpected(tracks.error());
  }
  yyjson_arr_iter track_iter;
  yyjson_arr_iter_init(*tracks, &track_iter);
  yyjson_val *track_val;
  std::size_t track_index = 0;
  while ((track_val = yyjson_arr_iter_next(&track_iter)) != nullptr) {
    const std::string trpath = path_index("/animation_tracks", track_index++);
    if (!yyjson_is_obj(track_val)) {
      return std::unexpected(err("type_mismatch", "Expected object", trpath));
    }
    static constexpr std::string_view kTrackKeys[] = {
        "id", "target_id", "property", "interpolation", "keyframes"};
    if (auto check = reject_unknown_keys(track_val, trpath, kTrackKeys);
        !check) {
      return std::unexpected(check.error());
    }
    AnimationTrack track;
    if (auto id = require_string(track_val, "id", trpath, 128); !id) {
      return std::unexpected(id.error());
    } else {
      track.id = *id;
    }
    if (auto target = require_string(track_val, "target_id", trpath, 128);
        !target) {
      return std::unexpected(target.error());
    } else {
      track.target_id = *target;
    }
    if (auto property = require_string(track_val, "property", trpath, 64);
        !property) {
      return std::unexpected(property.error());
    } else {
      track.property = *property;
    }
    auto keyframes = require_arr(track_val, "keyframes", trpath);
    if (!keyframes) {
      return std::unexpected(keyframes.error());
    }
    yyjson_arr_iter kf_iter;
    yyjson_arr_iter_init(*keyframes, &kf_iter);
    yyjson_val *kf_val;
    std::size_t kf_index = 0;
    while ((kf_val = yyjson_arr_iter_next(&kf_iter)) != nullptr) {
      const std::string kfpath =
          path_index(path_join(trpath, "keyframes"), kf_index++);
      if (!yyjson_is_obj(kf_val)) {
        return std::unexpected(err("type_mismatch", "Expected object", kfpath));
      }
      Keyframe keyframe;
      if (auto frame = require_uint(kf_val, "frame", kfpath, 0, 1000000000ULL);
          !frame) {
        return std::unexpected(frame.error());
      } else {
        keyframe.frame = *frame;
      }
      if (auto value = require_number(kf_val, "value", kfpath); !value) {
        return std::unexpected(value.error());
      } else {
        keyframe.value = *value;
      }
      track.keyframes.push_back(keyframe);
    }
    if (track.keyframes.empty()) {
      return std::unexpected(err("missing_field", "keyframes must be non-empty",
                                 path_join(trpath, "keyframes")));
    }
    doc.animation_tracks.push_back(std::move(track));
  }

  auto shots = require_arr(root, "shots", "");
  if (!shots) {
    return std::unexpected(shots.error());
  }
  if (yyjson_arr_size(*shots) < 1U) {
    return std::unexpected(err(
        "missing_field", "shots must contain at least one entry", "/shots"));
  }
  yyjson_arr_iter shot_iter;
  yyjson_arr_iter_init(*shots, &shot_iter);
  yyjson_val *shot_val;
  std::size_t shot_index = 0;
  while ((shot_val = yyjson_arr_iter_next(&shot_iter)) != nullptr) {
    const std::string spath = path_index("/shots", shot_index++);
    if (!yyjson_is_obj(shot_val)) {
      return std::unexpected(err("type_mismatch", "Expected object", spath));
    }
    static constexpr std::string_view kShotKeys[] = {
        "id", "start_frame", "duration_frames", "node_ids", "camera_node_id"};
    if (auto check = reject_unknown_keys(shot_val, spath, kShotKeys); !check) {
      return std::unexpected(check.error());
    }
    Shot shot;
    if (auto id = require_string(shot_val, "id", spath, 128); !id) {
      return std::unexpected(id.error());
    } else {
      shot.id = *id;
    }
    if (auto start =
            require_uint(shot_val, "start_frame", spath, 0, 1000000000ULL);
        !start) {
      return std::unexpected(start.error());
    } else {
      shot.start_frame = *start;
    }
    if (auto duration =
            require_uint(shot_val, "duration_frames", spath, 1, 1000000000ULL);
        !duration) {
      return std::unexpected(duration.error());
    } else {
      shot.duration_frames = *duration;
    }
    auto node_ids = require_arr(shot_val, "node_ids", spath);
    if (!node_ids) {
      return std::unexpected(node_ids.error());
    }
    yyjson_arr_iter id_iter;
    yyjson_arr_iter_init(*node_ids, &id_iter);
    yyjson_val *id_val;
    while ((id_val = yyjson_arr_iter_next(&id_iter)) != nullptr) {
      if (!yyjson_is_str(id_val)) {
        return std::unexpected(err("type_mismatch", "Expected string",
                                   path_join(spath, "node_ids")));
      }
      shot.node_ids.emplace_back(yyjson_get_str(id_val));
    }
    if (yyjson_val *camera = yyjson_obj_get(shot_val, "camera_node_id")) {
      if (!yyjson_is_str(camera)) {
        return std::unexpected(err("type_mismatch", "Expected string",
                                   path_join(spath, "camera_node_id")));
      }
      shot.camera_node_id = yyjson_get_str(camera);
    }
    doc.shots.push_back(std::move(shot));
  }

  if (yyjson_val *spans = yyjson_obj_get(root, "transition_spans")) {
    if (!yyjson_is_arr(spans)) {
      return std::unexpected(
          err("type_mismatch", "Expected array", "/transition_spans"));
    }
    yyjson_arr_iter span_iter;
    yyjson_arr_iter_init(spans, &span_iter);
    yyjson_val *span_val;
    std::size_t span_index = 0;
    while ((span_val = yyjson_arr_iter_next(&span_iter)) != nullptr) {
      const std::string spath = path_index("/transition_spans", span_index++);
      if (!yyjson_is_obj(span_val)) {
        return std::unexpected(err("type_mismatch", "Expected object", spath));
      }
      static constexpr std::string_view kSpanKeys[] = {
          "id",          "from_shot_id",    "to_shot_id",
          "start_frame", "duration_frames", "blend"};
      if (auto check = reject_unknown_keys(span_val, spath, kSpanKeys);
          !check) {
        return std::unexpected(check.error());
      }
      TransitionSpan span;
      if (auto id = require_string(span_val, "id", spath, 128); !id) {
        return std::unexpected(id.error());
      } else {
        span.id = *id;
      }
      if (auto from = require_string(span_val, "from_shot_id", spath, 128);
          !from) {
        return std::unexpected(from.error());
      } else {
        span.from_shot_id = *from;
      }
      if (auto to = require_string(span_val, "to_shot_id", spath, 128); !to) {
        return std::unexpected(to.error());
      } else {
        span.to_shot_id = *to;
      }
      if (auto start =
              require_uint(span_val, "start_frame", spath, 0, 1000000000ULL);
          !start) {
        return std::unexpected(start.error());
      } else {
        span.start_frame = *start;
      }
      if (auto duration =
              require_uint(span_val, "duration_frames", spath, 0, 1000000ULL);
          !duration) {
        return std::unexpected(duration.error());
      } else {
        span.duration_frames = *duration;
      }
      if (auto blend = require_string(span_val, "blend", spath, 32); !blend) {
        return std::unexpected(blend.error());
      } else {
        span.blend = *blend;
      }
      doc.transition_spans.push_back(std::move(span));
    }
  }

  auto provenance = require_obj(root, "provenance", "");
  if (!provenance) {
    return std::unexpected(provenance.error());
  }
  static constexpr std::string_view kProvKeys[] = {
      "project_sha256", "style_packages", "source_schema_version"};
  if (auto check = reject_unknown_keys(*provenance, "/provenance", kProvKeys);
      !check) {
    return std::unexpected(check.error());
  }
  if (auto sha =
          require_string(*provenance, "project_sha256", "/provenance", 64);
      !sha) {
    return std::unexpected(sha.error());
  } else {
    doc.provenance.project_sha256 = *sha;
  }
  auto packages = require_arr(*provenance, "style_packages", "/provenance");
  if (!packages) {
    return std::unexpected(packages.error());
  }
  yyjson_arr_iter pkg_iter;
  yyjson_arr_iter_init(*packages, &pkg_iter);
  yyjson_val *pkg_val;
  while ((pkg_val = yyjson_arr_iter_next(&pkg_iter)) != nullptr) {
    if (!yyjson_is_obj(pkg_val)) {
      return std::unexpected(err("type_mismatch", "Expected object",
                                 "/provenance/style_packages"));
    }
    StylePackage package;
    if (auto id =
            require_string(pkg_val, "id", "/provenance/style_packages", 128);
        !id) {
      return std::unexpected(id.error());
    } else {
      package.id = *id;
    }
    if (auto version = require_string(pkg_val, "version",
                                      "/provenance/style_packages", 64);
        !version) {
      return std::unexpected(version.error());
    } else {
      package.version = *version;
    }
    doc.provenance.style_packages.push_back(std::move(package));
  }

  return doc;
}

} // namespace

std::expected<SceneDocument, diagnostics::Error>
load_scene_json(std::span<const std::byte> bytes, const LoadOptions &options) {
  if (bytes.size() > options.limits.max_bytes) {
    return std::unexpected(
        err("limit_exceeded", "JSON input exceeds byte limit", "/"));
  }
  if (bytes.empty()) {
    return std::unexpected(err("invalid_json", "JSON input is empty", "/"));
  }

  // Copy into a mutable buffer: yyjson_read_opts takes char* without const.
  std::string buffer(reinterpret_cast<const char *>(bytes.data()),
                     bytes.size());
  yyjson_read_err read_err{};
  DocGuard guard{
      yyjson_read_opts(buffer.data(), buffer.size(), 0, nullptr, &read_err)};
  if (guard.doc == nullptr) {
    return std::unexpected(
        err("invalid_json",
            read_err.msg != nullptr ? read_err.msg : "JSON parse failed", "/"));
  }
  if (yyjson_doc_get_val_count(guard.doc) > options.limits.max_values) {
    return std::unexpected(
        err("limit_exceeded", "JSON value count exceeds limit", "/"));
  }
  return decode_scene(yyjson_doc_get_root(guard.doc), options);
}

std::expected<SceneDocument, diagnostics::Error>
load_scene_json(std::string_view text, const LoadOptions &options) {
  const auto *data = reinterpret_cast<const std::byte *>(text.data());
  return load_scene_json(std::span<const std::byte>(data, text.size()),
                         options);
}

std::expected<SceneDocument, diagnostics::Error>
load_scene_file(const std::filesystem::path &path, const LoadOptions &options) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::unexpected(
        err("io_error", "Cannot open Scene IR file", path.generic_string()));
  }
  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  if (size < 0) {
    return std::unexpected(
        err("io_error", "Cannot size Scene IR file", path.generic_string()));
  }
  if (static_cast<std::uint64_t>(size) > options.limits.max_bytes) {
    return std::unexpected(
        err("limit_exceeded", "JSON input exceeds byte limit", "/"));
  }
  input.seekg(0, std::ios::beg);
  std::string buffer(static_cast<std::size_t>(size), '\0');
  if (size > 0 &&
      !input.read(buffer.data(), static_cast<std::streamsize>(size))) {
    return std::unexpected(
        err("io_error", "Cannot read Scene IR file", path.generic_string()));
  }
  return load_scene_json(std::string_view(buffer), options);
}

} // namespace instantee::scene_ir
