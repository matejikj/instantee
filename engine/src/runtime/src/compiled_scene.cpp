#include <instantee/runtime/compiled_scene.hpp>

#include <limits>
#include <unordered_set>
#include <utility>

namespace instantee::runtime {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message,
                       std::string path = {}) {
  auto error = diagnostics::Error{diagnostics::ExitCode::invalid_input,
                                  diagnostics::ErrorKind::invalid_input, code,
                                  "runtime", message};
  if (!path.empty()) {
    error = std::move(error).with_json_path(std::move(path));
  }
  return error;
}

bool parent_graph_has_cycle(std::span<const RuntimeNode> nodes) {
  for (std::uint32_t start = 0; start < nodes.size(); ++start) {
    std::unordered_set<std::uint32_t> seen;
    std::optional<std::uint32_t> cursor = start;
    while (cursor.has_value()) {
      if (!seen.insert(*cursor).second) {
        return true;
      }
      cursor = nodes[*cursor].parent_index;
    }
  }
  return false;
}

} // namespace

std::optional<std::uint32_t>
CompiledScene::find_node_index(std::string_view id) const {
  const auto it = id_to_index_.find(std::string(id));
  if (it == id_to_index_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<std::string_view>
CompiledScene::node_id_at(std::uint32_t index) const noexcept {
  if (index >= node_ids_.size()) {
    return std::nullopt;
  }
  return node_ids_[index];
}

std::expected<std::unique_ptr<const CompiledScene>, diagnostics::Error>
make_compiled_scene(CompiledSceneDraft draft) {
  if (draft.node_ids.size() != draft.nodes.size()) {
    return std::unexpected(
        err("length_mismatch", "node_ids and nodes must have equal length"));
  }
  if (draft.nodes.size() >
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    return std::unexpected(
        err("resource_budget_exceeded", "Too many runtime nodes"));
  }

  auto scene = std::unique_ptr<CompiledScene>(new CompiledScene());
  scene->canvas_width_ = draft.canvas_width;
  scene->canvas_height_ = draft.canvas_height;
  scene->canvas_color_ = std::move(draft.canvas_color);
  scene->frame_rate_ = draft.frame_rate;
  scene->duration_frames_ = draft.duration_frames;
  scene->seed_ = draft.seed;
  scene->node_ids_ = std::move(draft.node_ids);
  scene->nodes_ = std::move(draft.nodes);
  scene->text_runs_ = std::move(draft.text_runs);
  scene->cameras_ = std::move(draft.cameras);
  scene->shots_ = std::move(draft.shots);

  scene->id_to_index_.reserve(scene->node_ids_.size());
  for (std::uint32_t index = 0; index < scene->node_ids_.size(); ++index) {
    const std::string &id = scene->node_ids_[index];
    if (id.empty()) {
      return std::unexpected(err("empty_id", "Runtime node id is empty",
                                 "/nodes/" + std::to_string(index) + "/id"));
    }
    if (!scene->id_to_index_.emplace(id, index).second) {
      return std::unexpected(err("duplicate_id", "Duplicate runtime node id",
                                 "/nodes/" + std::to_string(index) + "/id")
                                 .with_node(id));
    }

    const RuntimeNode &node = scene->nodes_[index];
    if (node.parent_index.has_value()) {
      if (*node.parent_index >= scene->nodes_.size()) {
        return std::unexpected(
            err("dangling_reference", "parent_index out of range",
                "/nodes/" + std::to_string(index) + "/parent_index"));
      }
      if (*node.parent_index == index) {
        return std::unexpected(
            err("cyclic_parent", "A node cannot be its own parent",
                "/nodes/" + std::to_string(index) + "/parent_index")
                .with_node(id));
      }
    }
    if (node.text_run_index.has_value() &&
        *node.text_run_index >= scene->text_runs_.size()) {
      return std::unexpected(
          err("dangling_reference", "text_run_index out of range",
              "/nodes/" + std::to_string(index) + "/text_run_index"));
    }
    if (node.camera_index.has_value() &&
        *node.camera_index >= scene->cameras_.size()) {
      return std::unexpected(
          err("dangling_reference", "camera_index out of range",
              "/nodes/" + std::to_string(index) + "/camera_index"));
    }
  }

  if (parent_graph_has_cycle(scene->nodes_)) {
    return std::unexpected(err(
        "cyclic_parent", "Runtime parent graph contains a cycle", "/nodes"));
  }

  for (std::size_t shot_index = 0; shot_index < scene->shots_.size();
       ++shot_index) {
    const RuntimeShot &shot = scene->shots_[shot_index];
    for (std::uint32_t node_index : shot.node_indices) {
      if (node_index >= scene->nodes_.size()) {
        return std::unexpected(
            err("dangling_reference", "shot node_index out of range",
                "/shots/" + std::to_string(shot_index) + "/node_indices"));
      }
    }
    if (shot.camera_node_index.has_value() &&
        *shot.camera_node_index >= scene->nodes_.size()) {
      return std::unexpected(
          err("dangling_reference", "camera_node_index out of range",
              "/shots/" + std::to_string(shot_index) + "/camera_node_index"));
    }
  }

  return std::unique_ptr<const CompiledScene>(std::move(scene));
}

} // namespace instantee::runtime
