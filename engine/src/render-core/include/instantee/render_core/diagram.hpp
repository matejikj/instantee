#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::render_core {

// Notebook/diagram primitives lowered onto shared DrawOps. Connectors bind to
// typed ports on nodes so translating/rotating a node updates edge endpoints
// without rewriting the edge authoring data.

enum class DiagramPort : std::uint8_t {
  center = 1,
  north,
  east,
  south,
  west,
};

enum class DiagramEdgeKind : std::uint8_t {
  line = 1,       // plain segment between ports
  arrow = 2,      // segment + arrow head at the destination
  orthogonal = 3, // one elbow: horizontal then vertical (or vice versa)
};

enum class DiagramNodeShape : std::uint8_t {
  rounded_rect = 1,
  ellipse = 2,
};

struct DiagramPoint {
  float x = 0;
  float y = 0;
};

struct DiagramNode {
  std::string id{};
  float cx = 0;
  float cy = 0;
  float width = 40;
  float height = 28;
  // Rotation around the node center (radians). Ports rotate with the node.
  float rotation_rad = 0;
  DiagramNodeShape shape = DiagramNodeShape::rounded_rect;
  float corner_radius = 4.0f;
  // Mask/reveal in [0, 1]: scales fill/stroke alpha (0 = hidden).
  float reveal = 1.0f;
  Rgba8 fill{240, 248, 255, 255};
  Rgba8 stroke{20, 40, 90, 255};
  float stroke_width = 1.5f;
};

struct DiagramEdge {
  std::string id{};
  std::string from_id{};
  std::string to_id{};
  DiagramPort from_port = DiagramPort::east;
  DiagramPort to_port = DiagramPort::west;
  DiagramEdgeKind kind = DiagramEdgeKind::arrow;
  float stroke_width = 2.0f;
  float head_length = 10.0f;
  float head_width = 8.0f;
  // Mask/reveal in [0, 1]: draws a prefix of the connector (0 = hidden).
  float reveal = 1.0f;
  Rgba8 color{20, 40, 90, 255};
};

struct DiagramLabel {
  std::string id{};
  // Empty node_id → absolute (x,y). Otherwise offset from the node center
  // after the node's transform.
  std::string node_id{};
  float x = 0;
  float y = 0;
  float width = 24;
  float height = 10;
  float reveal = 1.0f;
  Rgba8 color{20, 40, 90, 180};
};

struct DiagramGuide {
  std::string id{};
  bool horizontal = true;
  float position = 0; // y if horizontal, x if vertical
  float length = 100;
  float origin = 0; // x if horizontal, y if vertical
  Rgba8 color{180, 180, 200, 128};
  float stroke_width = 1.0f;
};

struct DiagramCallout {
  std::string id{};
  std::string target_id{}; // node id; stem attaches to a port
  DiagramPort target_port = DiagramPort::north;
  float box_cx = 0;
  float box_cy = 0;
  float box_w = 36;
  float box_h = 20;
  float reveal = 1.0f;
  Rgba8 fill{255, 250, 220, 255};
  Rgba8 stroke{20, 40, 90, 255};
};

struct DiagramScene {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  Rgba8 clear_color{255, 255, 255, 255};
  std::vector<DiagramNode> nodes{};
  std::vector<DiagramEdge> edges{};
  std::vector<DiagramLabel> labels{};
  std::vector<DiagramGuide> guides{};
  std::vector<DiagramCallout> callouts{};
};

struct DiagramResolvedEdge {
  std::string id{};
  DiagramPoint from{};
  DiagramPoint to{};
  // For orthogonal: elbow point. For others equals unused (NaN markers avoided
  // — from/to only matter).
  DiagramPoint mid{};
  bool has_mid = false;
  float reveal = 1.0f;
};

struct DiagramLayout {
  std::vector<DiagramResolvedEdge> edges{};
  std::vector<DrawOp> ops{};
};

[[nodiscard]] constexpr std::string_view
diagram_port_id(DiagramPort port) noexcept {
  switch (port) {
  case DiagramPort::center:
    return "center";
  case DiagramPort::north:
    return "north";
  case DiagramPort::east:
    return "east";
  case DiagramPort::south:
    return "south";
  case DiagramPort::west:
    return "west";
  }
  return "unknown";
}

// Port position in pass space after the node's translation/rotation.
[[nodiscard]] std::expected<DiagramPoint, diagnostics::Error>
diagram_port_point(const DiagramNode &node, DiagramPort port);

// Resolve connectors from the current node transforms and lower the scene to
// DrawOps. Re-running after moving/rotating a node updates edge endpoints.
[[nodiscard]] std::expected<DiagramLayout, diagnostics::Error>
layout_diagram(const DiagramScene &scene);

// Deterministic text dump for layout goldens (ports + resolved edges + ops).
[[nodiscard]] std::string
format_diagram_layout_golden(std::string_view label, const DiagramScene &scene,
                             const DiagramLayout &layout);

} // namespace instantee::render_core
