#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/diagram.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::render_core {

// Concept-diagram animation samples a DiagramScene over unit time. Topology and
// DrawOp lowering stay in layout_diagram — only authored state (reveals, poses,
// highlight colors) changes, so the renderer never needs a special path.

struct DiagramRevealStage {
  std::vector<std::string> edge_ids{};
  // Inclusive start / exclusive-feel end on unit time. Within the window the
  // edge reveal lerps 0→1; before start → 0; at/after end → 1.
  float start_t = 0;
  float end_t = 1;
};

struct DiagramNodePose {
  std::string id{};
  float cx = 0;
  float cy = 0;
  float rotation_rad = 0;
};

// Graph layout keyframe. Missing node ids keep the base scene pose (and still
// interpolate against neighbouring keys that do name them).
struct DiagramLayoutPose {
  float t = 0;
  std::vector<DiagramNodePose> nodes{};
};

struct DiagramPathHighlight {
  std::vector<std::string> edge_ids{};
  std::vector<std::string> node_ids{};
  float start_t = 0;
  float end_t = 1;
  Rgba8 color{200, 40, 50, 255};
  float stroke_width_scale = 1.75f;
};

struct DiagramGroupCompare {
  std::vector<std::string> group_a_node_ids{};
  std::vector<std::string> group_b_node_ids{};
  float start_t = 0;
  float end_t = 1;
  Rgba8 group_a_fill{190, 220, 255, 255};
  Rgba8 group_b_fill{255, 220, 180, 255};
  Rgba8 dim_fill{235, 235, 235, 255};
};

struct DiagramAnimationSpec {
  DiagramScene base{};
  std::vector<DiagramRevealStage> reveal_stages{};
  std::vector<DiagramLayoutPose>
      layout_poses{}; // must be sorted by ascending t
  DiagramPathHighlight active_path{};
  DiagramGroupCompare group_compare{};
};

struct DiagramAnimationSample {
  float t = 0;
  DiagramScene scene{};
  bool path_active = false;
  bool compare_active = false;
};

[[nodiscard]] std::expected<DiagramAnimationSample, diagnostics::Error>
sample_diagram_animation(const DiagramAnimationSpec &spec, float t);

// Multi-frame dump for the animation demo golden (state only — same layout
// path).
[[nodiscard]] std::expected<std::string, diagnostics::Error>
format_diagram_animation_demo(const DiagramAnimationSpec &spec,
                              std::span<const float> times);

} // namespace instantee::render_core
