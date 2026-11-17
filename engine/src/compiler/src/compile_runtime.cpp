#include <instantee/compiler/compile_runtime.hpp>

#include <instantee/compiler/validate_properties.hpp>
#include <instantee/core_model/properties.hpp>

#include <unordered_map>
#include <utility>

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

} // namespace instantee::compiler
