#include <instantee/compiler/compile_runtime.hpp>

#include <instantee/compiler/validate_properties.hpp>
#include <instantee/core_model/color.hpp>
#include <instantee/core_model/properties.hpp>
#include <instantee/core_model/time.hpp>
#include <instantee/runtime/timeline.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace instantee::compiler {
namespace {

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

runtime::RuntimeNodeKind map_kind(scene_ir::NodeKind kind) {
  switch (kind) {
  case scene_ir::NodeKind::group:
    return runtime::RuntimeNodeKind::group;
  case scene_ir::NodeKind::geometry:
    return runtime::RuntimeNodeKind::geometry;
  case scene_ir::NodeKind::text:
    return runtime::RuntimeNodeKind::text;
  case scene_ir::NodeKind::image:
    return runtime::RuntimeNodeKind::image;
  case scene_ir::NodeKind::puppet:
    return runtime::RuntimeNodeKind::puppet;
  case scene_ir::NodeKind::camera:
    return runtime::RuntimeNodeKind::camera;
  }
  return runtime::RuntimeNodeKind::group;
}

runtime::RuntimeTransform map_transform(const scene_ir::Transform &transform) {
  runtime::RuntimeTransform out;
  if (transform.translate) {
    out.translate_x = transform.translate->x;
    out.translate_y = transform.translate->y;
    out.translate_z = transform.translate->z;
  }
  if (transform.scale) {
    out.scale_x = transform.scale->x;
    out.scale_y = transform.scale->y;
    out.scale_z = transform.scale->z;
  }
  if (transform.rotation_z_rad) {
    out.rotation_z_rad = *transform.rotation_z_rad;
  }
  if (transform.opacity) {
    out.opacity = *transform.opacity;
  }
  if (transform.pivot) {
    out.pivot_x = transform.pivot->x;
    out.pivot_y = transform.pivot->y;
    out.pivot_z = transform.pivot->z;
  }
  return out;
}

} // namespace

std::expected<std::unique_ptr<const runtime::CompiledScene>, diagnostics::Error>
compile_runtime_scene(const scene_ir::SceneDocument &document) {
  // Unsupported animation bindings fail at compile, not during render.
  const auto property_registry = core::make_builtin_property_registry();
  if (auto properties =
          validate_animation_properties(document, property_registry);
      !properties) {
    return std::unexpected(properties.error());
  }

  std::unordered_map<std::string, std::uint32_t> node_index;
  node_index.reserve(document.nodes.size());
  for (std::uint32_t index = 0; index < document.nodes.size(); ++index) {
    if (!node_index.emplace(document.nodes[index].id, index).second) {
      return std::unexpected(err("duplicate_id",
                                 "Duplicate node id in Scene IR",
                                 "/nodes/" + std::to_string(index) + "/id")
                                 .with_node(document.nodes[index].id));
    }
  }

  std::unordered_map<std::string, std::uint32_t> text_run_index;
  text_run_index.reserve(document.text_runs.size());
  for (std::uint32_t index = 0; index < document.text_runs.size(); ++index) {
    text_run_index.emplace(document.text_runs[index].id, index);
  }

  std::unordered_map<std::string, std::uint32_t> camera_index;
  camera_index.reserve(document.cameras.size());
  for (std::uint32_t index = 0; index < document.cameras.size(); ++index) {
    camera_index.emplace(document.cameras[index].id, index);
  }

  std::unordered_map<std::string, const scene_ir::Geometry *> geometry_by_id;
  geometry_by_id.reserve(document.geometries.size());
  for (const scene_ir::Geometry &geom : document.geometries) {
    geometry_by_id.emplace(geom.id, &geom);
  }

  std::unordered_map<std::string, const scene_ir::Material *> material_by_id;
  material_by_id.reserve(document.materials.size());
  for (const scene_ir::Material &material : document.materials) {
    material_by_id.emplace(material.id, &material);
  }

  runtime::CompiledSceneDraft draft;
  draft.canvas_width = document.canvas_width;
  draft.canvas_height = document.canvas_height;
  draft.canvas_color = document.canvas_color;
  draft.frame_rate = document.frame_rate;
  draft.duration_frames = document.duration_frames;
  draft.seed = document.seed;
  draft.node_ids.reserve(document.nodes.size());
  draft.nodes.reserve(document.nodes.size());
  draft.text_runs.reserve(document.text_runs.size());
  draft.cameras.reserve(document.cameras.size());
  draft.shots.reserve(document.shots.size());

  for (const scene_ir::TextRun &run : document.text_runs) {
    runtime::RuntimeTextRun out;
    out.text = run.text;
    out.font_size_px = run.font_size_px;
    out.text_role = run.text_role;
    draft.text_runs.push_back(std::move(out));
  }

  for (const scene_ir::Camera &camera : document.cameras) {
    runtime::RuntimeCamera out;
    out.projection = camera.projection;
    out.near = camera.near;
    out.far = camera.far;
    out.fov_y_rad = camera.fov_y_rad;
    if (camera.transform) {
      out.transform = map_transform(*camera.transform);
    }
    draft.cameras.push_back(std::move(out));
  }

  for (std::size_t index = 0; index < document.nodes.size(); ++index) {
    const scene_ir::Node &node = document.nodes[index];
    draft.node_ids.push_back(node.id);
    runtime::RuntimeNode out;
    out.kind = map_kind(node.kind);
    out.layer_order = node.layer_order;
    out.visible = node.visible;
    if (node.transform) {
      out.transform = map_transform(*node.transform);
    }
    if (node.parent_id) {
      const auto it = node_index.find(*node.parent_id);
      if (it == node_index.end()) {
        return std::unexpected(
            err("dangling_reference", "Unknown parent_id",
                "/nodes/" + std::to_string(index) + "/parent_id")
                .with_node(node.id));
      }
      out.parent_index = it->second;
    }
    if (node.text_run_id) {
      const auto it = text_run_index.find(*node.text_run_id);
      if (it == text_run_index.end()) {
        return std::unexpected(
            err("dangling_reference", "Unknown text_run_id",
                "/nodes/" + std::to_string(index) + "/text_run_id")
                .with_node(node.id));
      }
      out.text_run_index = it->second;
    }
    if (node.camera_id) {
      const auto it = camera_index.find(*node.camera_id);
      if (it == camera_index.end()) {
        return std::unexpected(
            err("dangling_reference", "Unknown camera_id",
                "/nodes/" + std::to_string(index) + "/camera_id")
                .with_node(node.id));
      }
      out.camera_index = it->second;
    }

    // Preview proxy extents: geometry AABB, text estimate, else canvas fill for
    // image/puppet until atlas payloads are wired through runtime.
    if (node.kind == scene_ir::NodeKind::geometry && node.geometry_id) {
      if (const auto it = geometry_by_id.find(*node.geometry_id);
          it != geometry_by_id.end()) {
        const scene_ir::Geometry &geom = *it->second;
        const double width = std::max(1.0, geom.max_x - geom.min_x);
        const double height = std::max(1.0, geom.max_y - geom.min_y);
        out.local_x = static_cast<float>(geom.min_x);
        out.local_y = static_cast<float>(geom.min_y);
        out.local_width = static_cast<float>(width);
        out.local_height = static_cast<float>(height);
      }
    } else if (node.kind == scene_ir::NodeKind::text &&
               node.text_run_id.has_value()) {
      if (const auto it = text_run_index.find(*node.text_run_id);
          it != text_run_index.end()) {
        const scene_ir::TextRun &run = document.text_runs[it->second];
        const double size = std::max(run.font_size_px, 1.0);
        const double width =
            run.max_width_px.value_or(size * std::max<std::size_t>(run.text.size(), 1U) *
                                      0.6);
        out.local_x = 0;
        out.local_y = 0;
        out.local_width = static_cast<float>(std::max(width, size));
        out.local_height = static_cast<float>(size * 1.2);
      }
    } else if (node.kind == scene_ir::NodeKind::image ||
               node.kind == scene_ir::NodeKind::puppet) {
      out.local_x = 0;
      out.local_y = 0;
      out.local_width = static_cast<float>(std::max(document.canvas_width, 1U));
      out.local_height = static_cast<float>(std::max(document.canvas_height, 1U));
    }

    if (node.material_id) {
      if (const auto it = material_by_id.find(*node.material_id);
          it != material_by_id.end()) {
        const scene_ir::Material &material = *it->second;
        out.base_color = core::srgb_rgba_to_linear(
            material.r, material.g, material.b,
            std::clamp(material.a * material.opacity, 0.0, 1.0));
      }
    }

    draft.nodes.push_back(std::move(out));
  }

  for (std::size_t shot_index = 0; shot_index < document.shots.size();
       ++shot_index) {
    const scene_ir::Shot &shot = document.shots[shot_index];
    runtime::RuntimeShot out;
    out.id = shot.id;
    out.start_frame = shot.start_frame;
    out.duration_frames = shot.duration_frames;
    out.node_indices.reserve(shot.node_ids.size());
    for (const std::string &node_id : shot.node_ids) {
      const auto it = node_index.find(node_id);
      if (it == node_index.end()) {
        return std::unexpected(
            err("dangling_reference", "Unknown shot node id",
                "/shots/" + std::to_string(shot_index) + "/node_ids")
                .with_node(node_id));
      }
      out.node_indices.push_back(it->second);
    }
    if (shot.camera_node_id) {
      const auto it = node_index.find(*shot.camera_node_id);
      if (it == node_index.end()) {
        return std::unexpected(
            err("dangling_reference", "Unknown camera_node_id",
                "/shots/" + std::to_string(shot_index) + "/camera_node_id"));
      }
      out.camera_node_index = it->second;
    }
    draft.shots.push_back(std::move(out));
  }

  return runtime::make_compiled_scene(std::move(draft));
}

namespace {

[[nodiscard]] std::optional<runtime::AnimatableChannel>
channel_from_property(std::string_view property) noexcept {
  using runtime::AnimatableChannel;
  if (property == "opacity") {
    return AnimatableChannel::opacity;
  }
  if (property == "translate_x") {
    return AnimatableChannel::translate_x;
  }
  if (property == "translate_y") {
    return AnimatableChannel::translate_y;
  }
  if (property == "translate_z") {
    return AnimatableChannel::translate_z;
  }
  if (property == "scale_x") {
    return AnimatableChannel::scale_x;
  }
  if (property == "scale_y") {
    return AnimatableChannel::scale_y;
  }
  if (property == "scale_z") {
    return AnimatableChannel::scale_z;
  }
  if (property == "rotation_z_rad") {
    return AnimatableChannel::rotation_z_rad;
  }
  return std::nullopt;
}

} // namespace

std::expected<runtime::AnimationProgram, diagnostics::Error>
compile_animation_program(const scene_ir::SceneDocument &document,
                          const runtime::CompiledScene &scene) {
  std::unordered_map<std::string, std::uint32_t> index_by_id;
  const auto ids = scene.node_ids();
  index_by_id.reserve(ids.size());
  for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(ids.size()); ++i) {
    index_by_id.emplace(ids[i], i);
  }

  runtime::AnimationProgram program;
  program.scalar_bindings.reserve(document.animation_tracks.size());
  for (std::size_t track_index = 0;
       track_index < document.animation_tracks.size(); ++track_index) {
    const auto &track = document.animation_tracks[track_index];
    const auto channel = channel_from_property(track.property);
    if (!channel) {
      continue;
    }
    const auto node = index_by_id.find(track.target_id);
    if (node == index_by_id.end()) {
      return std::unexpected(
          err("dangling_reference", "Unknown animation target id",
              "/animation_tracks/" + std::to_string(track_index) + "/target_id")
              .with_node(track.target_id));
    }
    if (track.keyframes.empty()) {
      return std::unexpected(err(
          "empty_keyframes", "Animation track requires at least one keyframe",
          "/animation_tracks/" + std::to_string(track_index) + "/keyframes"));
    }
    std::vector<runtime::ScalarKeyframe> keys;
    keys.reserve(track.keyframes.size());
    for (const auto &key : track.keyframes) {
      keys.push_back(
          runtime::ScalarKeyframe{core::FrameIndex{key.frame}, key.value});
    }
    auto scalar = runtime::make_scalar_track(
        std::move(keys), runtime::TrackInterpolation::linear);
    if (!scalar) {
      return std::unexpected(scalar.error());
    }
    program.scalar_bindings.push_back(runtime::ScalarPropertyBinding{
        .node_index = node->second,
        .channel = *channel,
        .track = std::move(*scalar),
    });
  }
  return program;
}

} // namespace instantee::compiler
