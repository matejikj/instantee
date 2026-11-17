#include <instantee/compiler/compile.hpp>

#include <instantee/compiler/validate_properties.hpp>
#include <instantee/core_model/integer_encoding.hpp>
#include <instantee/core_model/properties.hpp>
#include <instantee/direction/directed_film.hpp>
#include <instantee/direction/motion_grammar.hpp>
#include <instantee/direction/semantic_intent.hpp>
#include <instantee/direction/style_package_registry.hpp>
#include <instantee/direction/track_precedence.hpp>
#include <instantee/scene_ir/canonicalize.hpp>
#include <instantee/scene_ir/graph_validate.hpp>
#include <instantee/scene_ir/load.hpp>

#include <chrono>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::compiler {
namespace {

using clock = std::chrono::steady_clock;

double ms_since(clock::time_point start) {
  return std::chrono::duration<double, std::milli>(clock::now() - start)
      .count();
}

diagnostics::Error err(std::string_view code, std::string_view message,
                       std::string path = {}) {
  auto error = diagnostics::Error{diagnostics::ExitCode::invalid_input,
                                  diagnostics::ErrorKind::invalid_input, code,
                                  "compile", message};
  if (!path.empty()) {
    error = std::move(error).with_json_path(std::move(path));
  }
  return error;
}

void collect_node_ids(const scene_ir::AuthoringNode &node,
                      std::vector<std::string_view> &ids) {
  ids.push_back(node.id);
  for (const auto &child : node.children) {
    collect_node_ids(child, ids);
  }
}

void append_nodes(const scene_ir::AuthoringNode &node, std::int32_t layer,
                  std::optional<std::string> parent_id,
                  std::vector<scene_ir::Node> &nodes,
                  std::vector<scene_ir::TextRun> &text_runs,
                  std::vector<std::string> &shot_node_ids,
                  std::string_view font_asset_id,
                  std::string_view material_id) {
  scene_ir::Node out;
  out.id = node.id;
  out.parent_id = std::move(parent_id);
  out.layer_order = layer;
  out.visible = node.visible;
  scene_ir::Transform transform;
  scene_ir::Vec3 translate;
  translate.x = node.translate_x.value_or(0);
  translate.y = node.translate_y.value_or(0);
  translate.z = 0;
  transform.translate = translate;
  transform.opacity = node.opacity.value_or(1.0);
  out.transform = transform;

  if (node.type == "text") {
    out.kind = scene_ir::NodeKind::text;
    scene_ir::TextRun run;
    run.id = node.id + "_text";
    run.text = node.text.value_or("");
    run.font_asset_id = std::string(font_asset_id);
    run.font_size_px = 48;
    if (node.role) {
      run.text_role = *node.role;
    }
    out.text_run_id = run.id;
    out.material_id = std::string(material_id);
    text_runs.push_back(std::move(run));
  } else if (node.type == "group") {
    out.kind = scene_ir::NodeKind::group;
  } else {
    out.kind = scene_ir::NodeKind::group;
  }
  const std::string self_id = out.id;
  shot_node_ids.push_back(out.id);
  nodes.push_back(std::move(out));
  std::int32_t child_layer = layer + 1;
  for (const auto &child : node.children) {
    append_nodes(child, child_layer++, self_id, nodes, text_runs, shot_node_ids,
                 font_asset_id, material_id);
  }
}

std::string style_id(std::string_view style) {
  const auto at = style.find('@');
  if (at == std::string_view::npos) {
    return std::string(style);
  }
  return std::string(style.substr(0, at));
}

std::string style_version(std::string_view style) {
  const auto at = style.find('@');
  if (at == std::string_view::npos) {
    return "1.0.0";
  }
  const std::string_view major = style.substr(at + 1);
  if (major.find('.') != std::string_view::npos) {
    return std::string(major);
  }
  return std::string(major) + ".0.0";
}

std::string map_authoring_property(std::string_view property) {
  if (property == "rotation_z") {
    return "rotation_z_rad";
  }
  return std::string(property);
}

void collect_author_claims(const scene_ir::AuthoringNode &node,
                           std::string_view shot_id,
                           std::vector<direction::TrackClaim> &claims) {
  std::size_t ordinal = 0;
  for (const auto &animation : node.animations) {
    direction::DirectedTrack track;
    track.id = std::string(shot_id) + "_author_" + node.id + "_" +
               animation.property + "_" + std::to_string(ordinal++);
    track.target_id = node.id;
    track.property = map_authoring_property(animation.property);
    track.keyframes.reserve(animation.keyframes.size());
    for (const auto &kf : animation.keyframes) {
      track.keyframes.push_back(
          direction::DirectedKeyframe{.frame = kf.frame, .value = kf.value});
    }
    claims.push_back(direction::TrackClaim{
        .track = std::move(track),
        .source = direction::TrackSource::explicit_author,
    });
  }
  for (const auto &child : node.children) {
    collect_author_claims(child, shot_id, claims);
  }
}

std::expected<void, diagnostics::Error>
resolve_shot_tracks(const scene_ir::AuthoringShot &shot, std::size_t index,
                    std::uint64_t start_frame, std::string_view package_id,
                    std::vector<scene_ir::AnimationTrack> &tracks) {
  std::vector<direction::TrackClaim> claims;
  for (const auto &node : shot.nodes) {
    collect_author_claims(node, shot.id, claims);
  }

  if (shot.intent.has_value()) {
    auto kind = direction::parse_intent_kind(shot.intent->type);
    if (!kind) {
      return std::unexpected(
          err("unsupported_intent",
              "Creative intent is not implemented by the CDS compiler",
              "/shots/" + std::to_string(index) + "/intent/type")
              .with_shot(shot.id));
    }

    std::vector<std::string_view> known;
    known.reserve(32);
    for (const auto &node : shot.nodes) {
      collect_node_ids(node, known);
    }

    std::vector<std::string_view> targets;
    targets.reserve(shot.intent->target_ids.size());
    for (const auto &id : shot.intent->target_ids) {
      targets.push_back(id);
    }

    const double importance = shot.intent->importance.value_or(0.5);
    auto semantic =
        direction::make_semantic_intent(*kind, targets, shot.intent->intensity,
                                        importance, std::nullopt, known);
    if (!semantic) {
      auto error = semantic.error();
      if (!error.context.json_path) {
        error = std::move(error).with_json_path(
            "/shots/" + std::to_string(index) + "/intent");
      }
      return std::unexpected(std::move(error).with_shot(shot.id));
    }

    // Grammar is resolved against the film Style Package; shot style_override
    // only changes precedence tagging, not the package binding (override
    // packages arrive in a later delivery).
    static const auto grammar =
        direction::make_builtin_motion_grammar_registry();
    auto lowered = direction::lower_semantic_intent(
        *semantic, shot.duration_frames, shot.id, package_id, grammar);
    if (!lowered) {
      return std::unexpected(lowered.error().with_shot(shot.id));
    }
    if (lowered->empty()) {
      return std::unexpected(
          err("unsupported_intent",
              "CDS lowering produced no tracks for a declared intent",
              "/shots/" + std::to_string(index) + "/intent")
              .with_shot(shot.id));
    }

    const direction::TrackSource cds_source =
        shot.style_override.has_value() ? direction::TrackSource::shot_override
                                        : direction::TrackSource::style_bible;
    for (auto &directed : *lowered) {
      claims.push_back(direction::TrackClaim{
          .track = std::move(directed),
          .source = cds_source,
      });
    }
  }

  if (claims.empty()) {
    return {};
  }

  auto resolved = direction::resolve_track_precedence(claims);
  if (!resolved) {
    return std::unexpected(
        std::move(resolved.error())
            .with_shot(shot.id)
            .with_json_path("/shots/" + std::to_string(index)));
  }

  for (auto &directed : *resolved) {
    scene_ir::AnimationTrack track;
    track.id = std::move(directed.id);
    track.target_id = std::move(directed.target_id);
    track.property = std::move(directed.property);
    track.keyframes.reserve(directed.keyframes.size());
    for (const auto &kf : directed.keyframes) {
      scene_ir::Keyframe out;
      out.frame = start_frame + kf.frame;
      out.value = kf.value;
      track.keyframes.push_back(out);
    }
    tracks.push_back(std::move(track));
  }
  return {};
}

} // namespace

std::expected<scene_ir::SceneDocument, diagnostics::Error>
compile_authoring(const scene_ir::AuthoringDocument &authoring,
                  const CompileOptions &options) {
  const auto package_name = style_id(authoring.style);
  const auto package_ver = style_version(authoring.style);
  auto registry = direction::make_builtin_style_package_registry();
  const auto engine = direction::make_default_engine_capabilities();
  auto package =
      registry.check_before_render(package_name, package_ver, engine);
  if (!package) {
    return std::unexpected(package.error());
  }

  scene_ir::SceneDocument doc;
  doc.schema_version = "1.0.0";
  doc.compiler_version = options.compiler_version;
  doc.canvas_width = authoring.canvas_width;
  doc.canvas_height = authoring.canvas_height;
  doc.canvas_color = authoring.canvas_color;
  doc.frame_rate = authoring.frame_rate;
  doc.seed = authoring.seed;

  scene_ir::Asset font;
  font.id = "font0";
  font.uri = "assets/example-font.ttf";
  font.sha256 =
      "0000000000000000000000000000000000000000000000000000000000000000";
  font.media_type = "font/ttf";
  font.color_profile = "untagged";
  doc.assets.push_back(font);

  scene_ir::Material material;
  material.id = "mat0";
  material.shader_feature = "solid-fill";
  material.opacity = 1.0;
  doc.materials.push_back(material);

  scene_ir::Camera camera;
  camera.id = "cam0";
  camera.projection = "orthographic";
  doc.cameras.push_back(camera);

  scene_ir::Node camera_node;
  camera_node.id = "main_cam";
  camera_node.kind = scene_ir::NodeKind::camera;
  camera_node.layer_order = 0;
  camera_node.camera_id = "cam0";
  camera_node.visible = true;
  doc.nodes.push_back(camera_node);

  std::uint64_t cursor = 0;
  std::int32_t layer = 1;
  for (std::size_t index = 0; index < authoring.shots.size(); ++index) {
    const auto &shot = authoring.shots[index];
    scene_ir::Shot out_shot;
    out_shot.id = shot.id;
    out_shot.start_frame = cursor;
    out_shot.duration_frames = shot.duration_frames;
    out_shot.camera_node_id = "main_cam";
    out_shot.node_ids.push_back("main_cam");
    for (const auto &node : shot.nodes) {
      append_nodes(node, layer++, std::nullopt, doc.nodes, doc.text_runs,
                   out_shot.node_ids, "font0", "mat0");
    }
    if (auto lowered = resolve_shot_tracks(shot, index, cursor, package_name,
                                           doc.animation_tracks);
        !lowered) {
      return std::unexpected(lowered.error());
    }
    cursor += shot.duration_frames;
    doc.shots.push_back(std::move(out_shot));
  }
  doc.duration_frames = core::FrameIndex{cursor};

  doc.provenance.project_sha256 =
      "0000000000000000000000000000000000000000000000000000000000000000";
  doc.provenance.source_schema_version = authoring.schema_version;
  scene_ir::StylePackage pkg;
  pkg.id = package->package_id;
  pkg.version = package->version;
  doc.provenance.style_packages.push_back(std::move(pkg));

  auto properties = core::make_builtin_property_registry();
  if (auto props = validate_animation_properties(doc, properties); !props) {
    return std::unexpected(props.error());
  }
  if (auto graph = scene_ir::validate_graph(doc); !graph) {
    return std::unexpected(graph.error());
  }
  return doc;
}

std::expected<std::string, diagnostics::Error>
scene_document_to_json(const scene_ir::SceneDocument &document) {
  std::ostringstream json;
  json << '{';
  json << "\"schema_version\":\"" << document.schema_version << "\",";
  json << "\"compiler_version\":\"" << document.compiler_version << "\",";
  json << "\"canvas\":{\"width\":" << document.canvas_width
       << ",\"height\":" << document.canvas_height << ",\"color\":\""
       << document.canvas_color << "\"},";
  json << "\"frame_rate\":{\"numerator\":" << document.frame_rate.numerator()
       << ",\"denominator\":" << document.frame_rate.denominator() << "},";
  json << "\"duration_frames\":" << document.duration_frames.value() << ",";
  if (document.seed > core::kJsSafeIntegerMax) {
    json << "\"seed\":\"" << core::Uint64Encoding::format_decimal(document.seed)
         << "\",";
  } else {
    json << "\"seed\":" << document.seed << ",";
  }
  json << "\"assets\":[";
  for (std::size_t i = 0; i < document.assets.size(); ++i) {
    if (i) {
      json << ',';
    }
    const auto &a = document.assets[i];
    json << "{\"id\":\"" << a.id << "\",\"uri\":\"" << a.uri
         << "\",\"sha256\":\"" << a.sha256 << "\",\"media_type\":\""
         << a.media_type << "\",\"color_profile\":\"" << a.color_profile
         << "\"}";
  }
  json << "],\"materials\":[";
  for (std::size_t i = 0; i < document.materials.size(); ++i) {
    if (i) {
      json << ',';
    }
    const auto &m = document.materials[i];
    json << "{\"id\":\"" << m.id << "\",\"shader_feature\":\""
         << m.shader_feature << "\",\"opacity\":" << m.opacity << "}";
  }
  json << "],\"geometry\":[],\"text_runs\":[";
  for (std::size_t i = 0; i < document.text_runs.size(); ++i) {
    if (i) {
      json << ',';
    }
    const auto &t = document.text_runs[i];
    json << "{\"id\":\"" << t.id << "\",\"text\":";
    json << '"';
    for (unsigned char ch : t.text) {
      if (ch == '"' || ch == '\\') {
        json << '\\' << static_cast<char>(ch);
      } else {
        json << static_cast<char>(ch);
      }
    }
    json << "\",\"font_asset_id\":\"" << t.font_asset_id
         << "\",\"font_size_px\":" << t.font_size_px;
    if (t.text_role) {
      json << ",\"text_role\":\"" << *t.text_role << '"';
    }
    json << '}';
  }
  json << "],\"cameras\":[";
  for (std::size_t i = 0; i < document.cameras.size(); ++i) {
    if (i) {
      json << ',';
    }
    const auto &c = document.cameras[i];
    json << "{\"id\":\"" << c.id << "\",\"projection\":\"" << c.projection
         << "\",\"near\":" << c.near << ",\"far\":" << c.far << '}';
  }
  json << "],\"rigs\":[],\"nodes\":[";
  for (std::size_t i = 0; i < document.nodes.size(); ++i) {
    if (i) {
      json << ',';
    }
    const auto &n = document.nodes[i];
    const char *kind = "group";
    switch (n.kind) {
    case scene_ir::NodeKind::geometry:
      kind = "geometry";
      break;
    case scene_ir::NodeKind::text:
      kind = "text";
      break;
    case scene_ir::NodeKind::image:
      kind = "image";
      break;
    case scene_ir::NodeKind::puppet:
      kind = "puppet";
      break;
    case scene_ir::NodeKind::camera:
      kind = "camera";
      break;
    case scene_ir::NodeKind::group:
      kind = "group";
      break;
    }
    json << "{\"id\":\"" << n.id << "\",\"kind\":\"" << kind
         << "\",\"layer_order\":" << n.layer_order
         << ",\"visible\":" << (n.visible ? "true" : "false");
    if (n.text_run_id) {
      json << ",\"text_run_id\":\"" << *n.text_run_id << '"';
    }
    if (n.material_id) {
      json << ",\"material_id\":\"" << *n.material_id << '"';
    }
    if (n.camera_id) {
      json << ",\"camera_id\":\"" << *n.camera_id << '"';
    }
    if (n.transform && n.transform->translate) {
      json << ",\"transform\":{\"translate\":{\"x\":"
           << n.transform->translate->x
           << ",\"y\":" << n.transform->translate->y
           << ",\"z\":" << n.transform->translate->z << "}";
      if (n.transform->opacity) {
        json << ",\"opacity\":" << *n.transform->opacity;
      }
      json << '}';
    }
    json << '}';
  }
  json << "],\"animation_tracks\":[";
  for (std::size_t i = 0; i < document.animation_tracks.size(); ++i) {
    if (i) {
      json << ',';
    }
    const auto &track = document.animation_tracks[i];
    json << "{\"id\":\"" << track.id << "\",\"target_id\":\"" << track.target_id
         << "\",\"property\":\"" << track.property << "\",\"keyframes\":[";
    for (std::size_t k = 0; k < track.keyframes.size(); ++k) {
      if (k) {
        json << ',';
      }
      json << "{\"frame\":" << track.keyframes[k].frame
           << ",\"value\":" << track.keyframes[k].value << '}';
    }
    json << "]}";
  }
  json << "],\"shots\":[";
  for (std::size_t i = 0; i < document.shots.size(); ++i) {
    if (i) {
      json << ',';
    }
    const auto &s = document.shots[i];
    json << "{\"id\":\"" << s.id << "\",\"start_frame\":" << s.start_frame
         << ",\"duration_frames\":" << s.duration_frames << ",\"node_ids\":[";
    for (std::size_t j = 0; j < s.node_ids.size(); ++j) {
      if (j) {
        json << ',';
      }
      json << '"' << s.node_ids[j] << '"';
    }
    json << ']';
    if (s.camera_node_id) {
      json << ",\"camera_node_id\":\"" << *s.camera_node_id << '"';
    }
    json << '}';
  }
  json << "],\"transition_spans\":[],\"provenance\":{\"project_sha256\":\""
       << document.provenance.project_sha256 << "\",\"style_packages\":[";
  for (std::size_t i = 0; i < document.provenance.style_packages.size(); ++i) {
    if (i) {
      json << ',';
    }
    const auto &p = document.provenance.style_packages[i];
    json << "{\"id\":\"" << p.id << "\",\"version\":\"" << p.version << "\"}";
  }
  json << ']';
  if (document.provenance.source_schema_version) {
    json << ",\"source_schema_version\":\""
         << *document.provenance.source_schema_version << '"';
  }
  json << "}}";
  return json.str();
}

std::expected<scene_ir::SceneDocument, diagnostics::Error>
compile_authoring_json(std::span<const std::byte> bytes,
                       const CompileOptions &options,
                       CompileStageTiming *timing) {
  CompileStageTiming local{};
  const auto t0 = clock::now();
  scene_ir::LoadOptions load_options;
  auto authoring = scene_ir::load_authoring_json(bytes, load_options);
  local.parse_ms = ms_since(t0);
  local.structural_ms = local.parse_ms;
  if (!authoring) {
    if (timing) {
      *timing = local;
    }
    return std::unexpected(authoring.error());
  }

  const auto t1 = clock::now();
  auto compiled = compile_authoring(*authoring, options);
  local.compile_ms = ms_since(t1);
  local.graph_ms = local.compile_ms;
  if (!compiled) {
    if (timing) {
      *timing = local;
    }
    return std::unexpected(compiled.error());
  }

  const auto t2 = clock::now();
  auto json = scene_document_to_json(*compiled);
  local.write_ms = ms_since(t2);
  if (!json) {
    if (timing) {
      *timing = local;
    }
    return std::unexpected(json.error());
  }
  const auto t3 = clock::now();
  auto digest = scene_ir::canonical_sha256_hex(*json);
  local.canonicalize_ms = ms_since(t3);
  local.ir_bytes = json->size();
  if (!digest) {
    if (timing) {
      *timing = local;
    }
    return std::unexpected(digest.error());
  }
  compiled->provenance.project_sha256 = *digest;
  if (timing) {
    *timing = local;
  }
  return compiled;
}

} // namespace instantee::compiler
