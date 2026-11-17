#include <instantee/direction/style_bible.hpp>

#include <yyjson.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <span>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message,
                       std::string_view json_path = {}) {
  auto out = diagnostics::Error{diagnostics::ExitCode::invalid_input,
                                diagnostics::ErrorKind::invalid_input, code,
                                "style_bible", message};
  if (!json_path.empty()) {
    out = std::move(out).with_json_path(std::string{json_path});
  }
  return out;
}

diagnostics::Error unsupported_schema() {
  return diagnostics::Error{diagnostics::ExitCode::incompatible_schema,
                            diagnostics::ErrorKind::incompatible_schema,
                            "unsupported_schema", "style_bible",
                            "Unsupported Style Bible schema_version"}
      .with_json_path("/schema_version");
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

bool key_allowed(std::string_view key,
                 std::span<const std::string_view> allowed) {
  return std::find(allowed.begin(), allowed.end(), key) != allowed.end();
}

std::expected<void, diagnostics::Error>
reject_unknown_keys(yyjson_val *obj, std::string_view json_path,
                    std::span<const std::string_view> allowed) {
  yyjson_obj_iter iter;
  yyjson_obj_iter_init(obj, &iter);
  while (yyjson_val *key = yyjson_obj_iter_next(&iter)) {
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

std::expected<std::string, diagnostics::Error>
require_string(yyjson_val *parent, std::string_view key, std::string_view path,
               std::size_t max_len) {
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
  if (text == nullptr || len == 0 || len > max_len) {
    return std::unexpected(err("invalid_string", "String length out of bounds",
                               path_join(path, key)));
  }
  return std::string{text, len};
}

std::expected<double, diagnostics::Error>
require_number(yyjson_val *parent, std::string_view key, std::string_view path,
               double min_v, double max_v) {
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
  if (!std::isfinite(number) || number < min_v || number > max_v) {
    return std::unexpected(err("out_of_range", "Number out of allowed range",
                               path_join(path, key)));
  }
  return number;
}

std::expected<std::uint32_t, diagnostics::Error>
require_uint(yyjson_val *parent, std::string_view key, std::string_view path,
             std::uint32_t min_v, std::uint32_t max_v) {
  auto number = require_number(parent, key, path, min_v, max_v);
  if (!number) {
    return std::unexpected(number.error());
  }
  if (*number != static_cast<double>(static_cast<std::uint32_t>(*number))) {
    return std::unexpected(
        err("type_mismatch", "Expected integer", path_join(path, key)));
  }
  return static_cast<std::uint32_t>(*number);
}

std::expected<FontRole, diagnostics::Error>
parse_font_role(std::string_view text, std::string_view path) {
  if (text == "headline") {
    return FontRole::headline;
  }
  if (text == "subhead") {
    return FontRole::subhead;
  }
  if (text == "body") {
    return FontRole::body;
  }
  if (text == "caption") {
    return FontRole::caption;
  }
  if (text == "mono") {
    return FontRole::mono;
  }
  return std::unexpected(err("invalid_enum", "Unknown font role", path));
}

std::expected<MotionEase, diagnostics::Error>
parse_ease(std::string_view text, std::string_view path) {
  if (text == "linear") {
    return MotionEase::linear;
  }
  if (text == "ease_in") {
    return MotionEase::ease_in;
  }
  if (text == "ease_out") {
    return MotionEase::ease_out;
  }
  if (text == "ease_in_out") {
    return MotionEase::ease_in_out;
  }
  if (text == "spring") {
    return MotionEase::spring;
  }
  return std::unexpected(err("invalid_enum", "Unknown ease", path));
}

std::expected<CameraFraming, diagnostics::Error>
parse_framing(std::string_view text, std::string_view path) {
  if (text == "static") {
    return CameraFraming::static_framing;
  }
  if (text == "push_in") {
    return CameraFraming::push_in;
  }
  if (text == "pull_out") {
    return CameraFraming::pull_out;
  }
  if (text == "pan") {
    return CameraFraming::pan;
  }
  if (text == "orbit") {
    return CameraFraming::orbit;
  }
  return std::unexpected(err("invalid_enum", "Unknown camera framing", path));
}

std::expected<SafeAreaMode, diagnostics::Error>
parse_safe_mode(std::string_view text, std::string_view path) {
  if (text == "reels_overlay") {
    return SafeAreaMode::reels_overlay;
  }
  if (text == "reels_clear") {
    return SafeAreaMode::reels_clear;
  }
  if (text == "full_frame") {
    return SafeAreaMode::full_frame;
  }
  return std::unexpected(err("invalid_enum", "Unknown safe-area mode", path));
}

template <typename Token, typename ParseFn>
std::expected<std::unordered_map<std::string, Token>, diagnostics::Error>
parse_token_map(yyjson_val *obj, std::string_view path, ParseFn parse_one) {
  if (!yyjson_is_obj(obj) || yyjson_obj_size(obj) == 0) {
    return std::unexpected(
        err("empty_tokens", "Token map must be a non-empty object", path));
  }
  std::unordered_map<std::string, Token> out;
  yyjson_obj_iter iter;
  yyjson_obj_iter_init(obj, &iter);
  while (yyjson_val *key = yyjson_obj_iter_next(&iter)) {
    yyjson_val *val = yyjson_obj_iter_get_val(key);
    const char *name = yyjson_get_str(key);
    if (name == nullptr || val == nullptr) {
      return std::unexpected(err("type_mismatch", "Invalid token entry", path));
    }
    const std::string id{name};
    auto parsed = parse_one(val, path_join(path, id));
    if (!parsed) {
      return std::unexpected(parsed.error());
    }
    if (!out.emplace(id, std::move(*parsed)).second) {
      return std::unexpected(
          err("duplicate_token", "Duplicate token id", path_join(path, id)));
    }
  }
  return out;
}

} // namespace

std::expected<StyleBible, diagnostics::Error>
load_style_bible_json(std::string_view text) {
  if (text.empty()) {
    return std::unexpected(err("empty_document", "Style Bible JSON is empty"));
  }
  yyjson_doc *doc = yyjson_read(text.data(), text.size(), YYJSON_READ_NOFLAG);
  if (doc == nullptr) {
    return std::unexpected(err("invalid_json", "JSON parse failed"));
  }
  struct DocGuard {
    yyjson_doc *owned = nullptr;
    ~DocGuard() { yyjson_doc_free(owned); }
  } guard{doc};

  yyjson_val *root = yyjson_doc_get_root(doc);
  if (root == nullptr || !yyjson_is_obj(root)) {
    return std::unexpected(err("type_mismatch", "Root must be an object"));
  }

  constexpr std::string_view kRootKeys[] = {"schema_version", "package_id",
                                            "version", "minimum_engine_version",
                                            "tokens"};
  if (auto unknown = reject_unknown_keys(root, "", kRootKeys); !unknown) {
    return std::unexpected(unknown.error());
  }

  StyleBible bible;
  if (auto schema = require_string(root, "schema_version", "", 32); !schema) {
    return std::unexpected(schema.error());
  } else if (*schema != kStyleBibleSchemaVersion) {
    return std::unexpected(unsupported_schema());
  } else {
    bible.schema_version_ = std::move(*schema);
  }

  if (auto package = require_string(root, "package_id", "", 64); !package) {
    return std::unexpected(package.error());
  } else {
    bible.package_id_ = std::move(*package);
  }
  if (auto version = require_string(root, "version", "", 64); !version) {
    return std::unexpected(version.error());
  } else {
    bible.version_ = std::move(*version);
  }
  if (auto engine = require_string(root, "minimum_engine_version", "", 64);
      !engine) {
    return std::unexpected(engine.error());
  } else {
    bible.minimum_engine_version_ = std::move(*engine);
  }

  auto tokens = require_obj(root, "tokens", "");
  if (!tokens) {
    return std::unexpected(tokens.error());
  }
  constexpr std::string_view kTokenKeys[] = {"colors",   "fonts",  "materials",
                                             "spacing",  "motion", "camera",
                                             "safe_area"};
  if (auto unknown = reject_unknown_keys(*tokens, "/tokens", kTokenKeys);
      !unknown) {
    return std::unexpected(unknown.error());
  }

  auto colors_obj = require_obj(*tokens, "colors", "/tokens");
  if (!colors_obj) {
    return std::unexpected(colors_obj.error());
  }
  auto colors = parse_token_map<ColorToken>(
      *colors_obj, "/tokens/colors",
      [](yyjson_val *val, std::string_view path)
          -> std::expected<ColorToken, diagnostics::Error> {
        constexpr std::string_view keys[] = {"space", "r", "g", "b", "a"};
        if (auto unknown = reject_unknown_keys(val, path, keys); !unknown) {
          return std::unexpected(unknown.error());
        }
        auto space = require_string(val, "space", path, 32);
        if (!space) {
          return std::unexpected(space.error());
        }
        if (*space != "linear-rgba") {
          return std::unexpected(err("invalid_enum", "Unsupported color space",
                                     path_join(path, "space")));
        }
        ColorToken token;
        auto r = require_number(val, "r", path, 0.0, 1.0);
        auto g = require_number(val, "g", path, 0.0, 1.0);
        auto b = require_number(val, "b", path, 0.0, 1.0);
        auto a = require_number(val, "a", path, 0.0, 1.0);
        if (!r || !g || !b || !a) {
          return std::unexpected((!r   ? r.error()
                                  : !g ? g.error()
                                  : !b ? b.error()
                                       : a.error()));
        }
        token.value = core::LinearRgba{*r, *g, *b, *a};
        return token;
      });
  if (!colors) {
    return std::unexpected(colors.error());
  }
  bible.colors_ = std::move(*colors);

  auto fonts_obj = require_obj(*tokens, "fonts", "/tokens");
  if (!fonts_obj) {
    return std::unexpected(fonts_obj.error());
  }
  auto fonts = parse_token_map<FontToken>(
      *fonts_obj, "/tokens/fonts",
      [](yyjson_val *val, std::string_view path)
          -> std::expected<FontToken, diagnostics::Error> {
        constexpr std::string_view keys[] = {"family", "role", "size_scale"};
        if (auto unknown = reject_unknown_keys(val, path, keys); !unknown) {
          return std::unexpected(unknown.error());
        }
        FontToken token;
        auto family = require_string(val, "family", path, 128);
        if (!family) {
          return std::unexpected(family.error());
        }
        token.family = std::move(*family);
        auto role = require_string(val, "role", path, 32);
        if (!role) {
          return std::unexpected(role.error());
        }
        auto parsed_role = parse_font_role(*role, path_join(path, "role"));
        if (!parsed_role) {
          return std::unexpected(parsed_role.error());
        }
        token.role = *parsed_role;
        auto scale = require_number(val, "size_scale", path, 0.000001, 16.0);
        if (!scale) {
          return std::unexpected(scale.error());
        }
        token.size_scale = *scale;
        return token;
      });
  if (!fonts) {
    return std::unexpected(fonts.error());
  }
  bible.fonts_ = std::move(*fonts);

  auto materials_obj = require_obj(*tokens, "materials", "/tokens");
  if (!materials_obj) {
    return std::unexpected(materials_obj.error());
  }
  auto materials = parse_token_map<MaterialToken>(
      *materials_obj, "/tokens/materials",
      [](yyjson_val *val, std::string_view path)
          -> std::expected<MaterialToken, diagnostics::Error> {
        constexpr std::string_view keys[] = {"shader_feature", "opacity",
                                             "parameters"};
        if (auto unknown = reject_unknown_keys(val, path, keys); !unknown) {
          return std::unexpected(unknown.error());
        }
        MaterialToken token;
        auto feature = require_string(val, "shader_feature", path, 64);
        if (!feature) {
          return std::unexpected(feature.error());
        }
        token.shader_feature = std::move(*feature);
        auto opacity = require_number(val, "opacity", path, 0.0, 1.0);
        if (!opacity) {
          return std::unexpected(opacity.error());
        }
        token.opacity = *opacity;
        if (yyjson_val *params = yyjson_obj_get(val, "parameters")) {
          if (!yyjson_is_obj(params)) {
            return std::unexpected(err("type_mismatch", "Expected object",
                                       path_join(path, "parameters")));
          }
          yyjson_obj_iter iter;
          yyjson_obj_iter_init(params, &iter);
          while (yyjson_val *key = yyjson_obj_iter_next(&iter)) {
            yyjson_val *pvalue = yyjson_obj_iter_get_val(key);
            const char *pname = yyjson_get_str(key);
            if (pname == nullptr || pvalue == nullptr ||
                !yyjson_is_num(pvalue)) {
              return std::unexpected(err("type_mismatch",
                                         "Material parameters must be numbers",
                                         path_join(path, "parameters")));
            }
            const double number = yyjson_get_num(pvalue);
            if (!std::isfinite(number)) {
              return std::unexpected(err("out_of_range",
                                         "Material parameter must be finite",
                                         path_join(path, "parameters")));
            }
            token.parameters.emplace(pname, number);
          }
        }
        return token;
      });
  if (!materials) {
    return std::unexpected(materials.error());
  }
  bible.materials_ = std::move(*materials);

  auto spacing_obj = require_obj(*tokens, "spacing", "/tokens");
  if (!spacing_obj) {
    return std::unexpected(spacing_obj.error());
  }
  auto spacing = parse_token_map<SpacingToken>(
      *spacing_obj, "/tokens/spacing",
      [](yyjson_val *val, std::string_view path)
          -> std::expected<SpacingToken, diagnostics::Error> {
        constexpr std::string_view keys[] = {"px"};
        if (auto unknown = reject_unknown_keys(val, path, keys); !unknown) {
          return std::unexpected(unknown.error());
        }
        auto px = require_number(val, "px", path, 0.0, 4096.0);
        if (!px) {
          return std::unexpected(px.error());
        }
        return SpacingToken{.px = *px};
      });
  if (!spacing) {
    return std::unexpected(spacing.error());
  }
  bible.spacing_ = std::move(*spacing);

  auto motion_obj = require_obj(*tokens, "motion", "/tokens");
  if (!motion_obj) {
    return std::unexpected(motion_obj.error());
  }
  auto motion = parse_token_map<MotionToken>(
      *motion_obj, "/tokens/motion",
      [](yyjson_val *val, std::string_view path)
          -> std::expected<MotionToken, diagnostics::Error> {
        constexpr std::string_view keys[] = {"ease", "duration_frames",
                                             "intensity", "stagger_frames"};
        if (auto unknown = reject_unknown_keys(val, path, keys); !unknown) {
          return std::unexpected(unknown.error());
        }
        MotionToken token;
        auto ease = require_string(val, "ease", path, 32);
        if (!ease) {
          return std::unexpected(ease.error());
        }
        auto parsed_ease = parse_ease(*ease, path_join(path, "ease"));
        if (!parsed_ease) {
          return std::unexpected(parsed_ease.error());
        }
        token.ease = *parsed_ease;
        auto duration = require_uint(val, "duration_frames", path, 1, 18000);
        if (!duration) {
          return std::unexpected(duration.error());
        }
        token.duration_frames = *duration;
        auto intensity = require_number(val, "intensity", path, 0.0, 1.0);
        if (!intensity) {
          return std::unexpected(intensity.error());
        }
        token.intensity = *intensity;
        if (yyjson_obj_get(val, "stagger_frames") != nullptr) {
          auto stagger = require_uint(val, "stagger_frames", path, 0, 600);
          if (!stagger) {
            return std::unexpected(stagger.error());
          }
          token.stagger_frames = *stagger;
        }
        return token;
      });
  if (!motion) {
    return std::unexpected(motion.error());
  }
  bible.motion_ = std::move(*motion);

  auto camera_obj = require_obj(*tokens, "camera", "/tokens");
  if (!camera_obj) {
    return std::unexpected(camera_obj.error());
  }
  auto camera = parse_token_map<CameraToken>(
      *camera_obj, "/tokens/camera",
      [](yyjson_val *val, std::string_view path)
          -> std::expected<CameraToken, diagnostics::Error> {
        constexpr std::string_view keys[] = {"framing", "ease"};
        if (auto unknown = reject_unknown_keys(val, path, keys); !unknown) {
          return std::unexpected(unknown.error());
        }
        CameraToken token;
        auto framing = require_string(val, "framing", path, 32);
        if (!framing) {
          return std::unexpected(framing.error());
        }
        auto parsed = parse_framing(*framing, path_join(path, "framing"));
        if (!parsed) {
          return std::unexpected(parsed.error());
        }
        token.framing = *parsed;
        if (yyjson_obj_get(val, "ease") != nullptr) {
          auto ease = require_string(val, "ease", path, 32);
          if (!ease) {
            return std::unexpected(ease.error());
          }
          auto parsed_ease = parse_ease(*ease, path_join(path, "ease"));
          if (!parsed_ease) {
            return std::unexpected(parsed_ease.error());
          }
          token.ease = *parsed_ease;
        }
        return token;
      });
  if (!camera) {
    return std::unexpected(camera.error());
  }
  bible.camera_ = std::move(*camera);

  auto safe = require_obj(*tokens, "safe_area", "/tokens");
  if (!safe) {
    return std::unexpected(safe.error());
  }
  constexpr std::string_view kSafeKeys[] = {"mode", "top_px", "bottom_px",
                                            "left_px", "right_px"};
  if (auto unknown = reject_unknown_keys(*safe, "/tokens/safe_area", kSafeKeys);
      !unknown) {
    return std::unexpected(unknown.error());
  }
  auto mode = require_string(*safe, "mode", "/tokens/safe_area", 32);
  if (!mode) {
    return std::unexpected(mode.error());
  }
  auto parsed_mode = parse_safe_mode(*mode, "/tokens/safe_area/mode");
  if (!parsed_mode) {
    return std::unexpected(parsed_mode.error());
  }
  bible.safe_area_.mode = *parsed_mode;
  auto top = require_number(*safe, "top_px", "/tokens/safe_area", 0.0, 960.0);
  auto bottom =
      require_number(*safe, "bottom_px", "/tokens/safe_area", 0.0, 960.0);
  auto left = require_number(*safe, "left_px", "/tokens/safe_area", 0.0, 540.0);
  auto right =
      require_number(*safe, "right_px", "/tokens/safe_area", 0.0, 540.0);
  if (!top || !bottom || !left || !right) {
    return std::unexpected((!top      ? top.error()
                            : !bottom ? bottom.error()
                            : !left   ? left.error()
                                      : right.error()));
  }
  bible.safe_area_.top_px = *top;
  bible.safe_area_.bottom_px = *bottom;
  bible.safe_area_.left_px = *left;
  bible.safe_area_.right_px = *right;

  return bible;
}

std::expected<StyleBible, diagnostics::Error>
load_style_bible_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::unexpected(
        err("io_error", "Cannot open Style Bible file", path.string()));
  }
  std::string text{std::istreambuf_iterator<char>{input},
                   std::istreambuf_iterator<char>{}};
  if (!input.good() && !input.eof()) {
    return std::unexpected(
        err("io_error", "Cannot read Style Bible file", path.string()));
  }
  return load_style_bible_json(text);
}

} // namespace instantee::direction
