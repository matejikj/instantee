#include <instantee/scene_ir/graph_validate.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace instantee::scene_ir {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message,
                       std::string json_path) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "scene_ir", message}
      .with_json_path(std::move(json_path));
}

template <typename T>
std::expected<std::unordered_set<std::string>, diagnostics::Error>
unique_ids(const std::vector<T> &items, std::string_view kind,
           std::string_view array_path) {
  std::unordered_set<std::string> ids;
  ids.reserve(items.size());
  for (std::size_t index = 0; index < items.size(); ++index) {
    const std::string &id = items[index].id;
    if (!ids.insert(id).second) {
      return std::unexpected(
          err("duplicate_id", "Duplicate identifier",
              std::string(array_path) + "/" + std::to_string(index) + "/id"));
    }
    (void)kind;
  }
  return ids;
}

} // namespace

std::expected<void, diagnostics::Error>
validate_graph(const SceneDocument &document) {
  auto assets = unique_ids(document.assets, "asset", "/assets");
  if (!assets) {
    return std::unexpected(assets.error());
  }
  auto materials = unique_ids(document.materials, "material", "/materials");
  if (!materials) {
    return std::unexpected(materials.error());
  }
  std::unordered_set<std::string> geometry(document.geometry_ids.begin(),
                                           document.geometry_ids.end());
  if (geometry.size() != document.geometry_ids.size()) {
    return std::unexpected(
        err("duplicate_id", "Duplicate geometry identifier", "/geometry"));
  }
  auto text_runs = unique_ids(document.text_runs, "text_run", "/text_runs");
  if (!text_runs) {
    return std::unexpected(text_runs.error());
  }
  auto cameras = unique_ids(document.cameras, "camera", "/cameras");
  if (!cameras) {
    return std::unexpected(cameras.error());
  }
  auto rigs = unique_ids(document.rigs, "rig", "/rigs");
  if (!rigs) {
    return std::unexpected(rigs.error());
  }
  auto nodes = unique_ids(document.nodes, "node", "/nodes");
  if (!nodes) {
    return std::unexpected(nodes.error());
  }
  auto tracks = unique_ids(document.animation_tracks, "animation_track",
                           "/animation_tracks");
  if (!tracks) {
    return std::unexpected(tracks.error());
  }
  auto shots = unique_ids(document.shots, "shot", "/shots");
  if (!shots) {
    return std::unexpected(shots.error());
  }

  std::unordered_map<std::string, std::optional<std::string>> parents;
  parents.reserve(document.nodes.size());
  for (std::size_t index = 0; index < document.nodes.size(); ++index) {
    const Node &node = document.nodes[index];
    const std::string path = "/nodes/" + std::to_string(index);
    if (node.parent_id.has_value()) {
      if (!nodes->contains(*node.parent_id)) {
        return std::unexpected(err("dangling_reference", "Unknown parent_id",
                                   path + "/parent_id"));
      }
    }
    if (node.geometry_id.has_value() && !geometry.contains(*node.geometry_id)) {
      return std::unexpected(err("dangling_reference", "Unknown geometry_id",
                                 path + "/geometry_id"));
    }
    if (node.text_run_id.has_value() &&
        !text_runs->contains(*node.text_run_id)) {
      return std::unexpected(err("dangling_reference", "Unknown text_run_id",
                                 path + "/text_run_id"));
    }
    if (node.asset_id.has_value() && !assets->contains(*node.asset_id)) {
      return std::unexpected(
          err("dangling_reference", "Unknown asset_id", path + "/asset_id"));
    }
    if (node.material_id.has_value() &&
        !materials->contains(*node.material_id)) {
      return std::unexpected(err("dangling_reference", "Unknown material_id",
                                 path + "/material_id"));
    }
    if (node.camera_id.has_value() && !cameras->contains(*node.camera_id)) {
      return std::unexpected(
          err("dangling_reference", "Unknown camera_id", path + "/camera_id"));
    }
    if (node.rig_id.has_value() && !rigs->contains(*node.rig_id)) {
      return std::unexpected(
          err("dangling_reference", "Unknown rig_id", path + "/rig_id"));
    }
    parents.emplace(node.id, node.parent_id);
  }

  for (const Node &node : document.nodes) {
    std::unordered_set<std::string> seen;
    std::optional<std::string> cursor = node.id;
    while (cursor.has_value()) {
      if (!seen.insert(*cursor).second) {
        return std::unexpected(err(
            "cyclic_parent", "Node parent graph contains a cycle", "/nodes"));
      }
      const auto it = parents.find(*cursor);
      if (it == parents.end()) {
        break;
      }
      cursor = it->second;
    }
  }

  for (std::size_t index = 0; index < document.text_runs.size(); ++index) {
    const TextRun &run = document.text_runs[index];
    if (!assets->contains(run.font_asset_id)) {
      return std::unexpected(
          err("dangling_reference", "Unknown font_asset_id",
              "/text_runs/" + std::to_string(index) + "/font_asset_id"));
    }
  }

  const std::uint64_t duration = document.duration_frames.value();
  for (std::size_t index = 0; index < document.animation_tracks.size();
       ++index) {
    const AnimationTrack &track = document.animation_tracks[index];
    const std::string path = "/animation_tracks/" + std::to_string(index);
    if (!nodes->contains(track.target_id)) {
      return std::unexpected(err("dangling_reference", "Unknown track target",
                                 path + "/target_id"));
    }
    for (std::size_t kf = 1; kf < track.keyframes.size(); ++kf) {
      if (track.keyframes[kf].frame <= track.keyframes[kf - 1].frame) {
        return std::unexpected(err("non_increasing_keyframes",
                                   "Keyframes must be strictly increasing",
                                   path + "/keyframes"));
      }
    }
    if (track.keyframes.back().frame >= duration) {
      return std::unexpected(err("keyframe_out_of_range",
                                 "Keyframe is outside duration_frames",
                                 path + "/keyframes"));
    }
  }

  std::uint64_t cursor = 0;
  for (std::size_t index = 0; index < document.shots.size(); ++index) {
    const Shot &shot = document.shots[index];
    const std::string path = "/shots/" + std::to_string(index);
    if (shot.start_frame != cursor) {
      return std::unexpected(err("shot_timeline_gap",
                                 "Shots must form a contiguous timeline",
                                 path + "/start_frame"));
    }
    for (const std::string &node_id : shot.node_ids) {
      if (!nodes->contains(node_id)) {
        return std::unexpected(err("dangling_reference", "Unknown shot node_id",
                                   path + "/node_ids"));
      }
    }
    if (shot.camera_node_id.has_value()) {
      if (!nodes->contains(*shot.camera_node_id)) {
        return std::unexpected(err("dangling_reference",
                                   "Unknown camera_node_id",
                                   path + "/camera_node_id"));
      }
    }
    cursor += shot.duration_frames;
  }
  if (cursor != duration) {
    return std::unexpected(err("shot_duration_mismatch",
                               "Shot durations do not cover duration_frames",
                               "/shots"));
  }

  for (std::size_t index = 0; index < document.transition_spans.size();
       ++index) {
    const TransitionSpan &span = document.transition_spans[index];
    const std::string path = "/transition_spans/" + std::to_string(index);
    if (!shots->contains(span.from_shot_id) ||
        !shots->contains(span.to_shot_id)) {
      return std::unexpected(
          err("dangling_reference", "Unknown transition shot", path));
    }
  }

  return {};
}

} // namespace instantee::scene_ir
