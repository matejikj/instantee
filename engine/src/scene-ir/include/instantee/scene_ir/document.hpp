#pragma once

#include <instantee/core_model/time.hpp>
#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace instantee::scene_ir {

struct Vec3 {
  double x = 0;
  double y = 0;
  double z = 0;
};

struct Transform {
  std::optional<Vec3> translate;
  std::optional<Vec3> scale;
  std::optional<double> rotation_z_rad;
  std::optional<double> opacity;
  std::optional<Vec3> pivot;
};

struct Asset {
  std::string id;
  std::string uri;
  std::string sha256;
  std::string media_type;
  std::string color_profile = "untagged";
};

struct Material {
  std::string id;
  std::string shader_feature;
  double opacity = 1.0;
};

struct TextRun {
  std::string id;
  std::string text;
  std::string font_asset_id;
  double font_size_px = 0;
  std::optional<double> max_width_px;
  std::optional<std::string> text_role;
};

struct Camera {
  std::string id;
  std::string projection = "orthographic";
  double near = 0.1;
  double far = 1000.0;
  std::optional<double> fov_y_rad;
  std::optional<Transform> transform;
};

struct Rig {
  std::string id;
  std::string format_version;
  std::string payload_ref;
};

enum class NodeKind : std::uint8_t {
  group,
  geometry,
  text,
  image,
  puppet,
  camera
};

struct Node {
  std::string id;
  NodeKind kind = NodeKind::group;
  std::optional<std::string> parent_id;
  std::int32_t layer_order = 0;
  std::optional<Transform> transform;
  std::optional<std::string> geometry_id;
  std::optional<std::string> text_run_id;
  std::optional<std::string> asset_id;
  std::optional<std::string> material_id;
  std::optional<std::string> camera_id;
  std::optional<std::string> rig_id;
  bool visible = true;
};

struct Keyframe {
  std::uint64_t frame = 0;
  double value = 0;
};

struct AnimationTrack {
  std::string id;
  std::string target_id;
  std::string property;
  std::vector<Keyframe> keyframes;
};

struct Shot {
  std::string id;
  std::uint64_t start_frame = 0;
  std::uint64_t duration_frames = 0;
  std::vector<std::string> node_ids;
  std::optional<std::string> camera_node_id;
};

struct TransitionSpan {
  std::string id;
  std::string from_shot_id;
  std::string to_shot_id;
  std::uint64_t start_frame = 0;
  std::uint64_t duration_frames = 0;
  std::string blend = "cut";
};

struct StylePackage {
  std::string id;
  std::string version;
};

struct Provenance {
  std::string project_sha256;
  std::vector<StylePackage> style_packages;
  std::optional<std::string> source_schema_version;
};

// Platform-neutral Scene IR value document produced by the loader.
struct SceneDocument {
  std::string schema_version;
  std::string compiler_version;
  std::uint32_t canvas_width = 0;
  std::uint32_t canvas_height = 0;
  std::string canvas_color = "rec709-sdr";
  core::FrameRate frame_rate = core::FrameRate::media_30_1();
  core::FrameIndex duration_frames{};
  std::uint64_t seed = 0;
  std::vector<Asset> assets;
  std::vector<Material> materials;
  std::vector<std::string> geometry_ids;
  std::vector<TextRun> text_runs;
  std::vector<Camera> cameras;
  std::vector<Rig> rigs;
  std::vector<Node> nodes;
  std::vector<AnimationTrack> animation_tracks;
  std::vector<Shot> shots;
  std::vector<TransitionSpan> transition_spans;
  Provenance provenance;
};

} // namespace instantee::scene_ir
