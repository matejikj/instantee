#include <instantee/render_core/diagram.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace instantee::render_core {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "diagram", message};
}

[[nodiscard]] bool finite_point(float x, float y) noexcept {
  return std::isfinite(x) && std::isfinite(y);
}

[[nodiscard]] float clamp01(float v) noexcept {
  if (!(v > 0.0f)) {
    return 0.0f;
  }
  if (v > 1.0f) {
    return 1.0f;
  }
  return v;
}

[[nodiscard]] Rgba8 scale_alpha(Rgba8 c, float reveal) noexcept {
  const float a = static_cast<float>(c.a) * clamp01(reveal);
  c.a = static_cast<std::uint8_t>(std::lround(std::min(255.0f, a)));
  return c;
}

[[nodiscard]] DrawOp make_line(float x0, float y0, float x1, float y1,
                               float width, Rgba8 color) {
  return DrawOp{.kind = DrawOpKind::stroke_line,
                .rect = {.x = x0, .y = y0, .width = 0, .height = 0},
                .x1 = x1,
                .y1 = y1,
                .color = color,
                .stroke_width = width};
}

[[nodiscard]] DrawOp make_fill_rect(Rect2f bounds, Rgba8 color) {
  return DrawOp{.kind = DrawOpKind::fill_rect, .rect = bounds, .color = color};
}

[[nodiscard]] DrawOp make_fill_rounded(Rect2f bounds, float radius,
                                       Rgba8 color) {
  return DrawOp{.kind = DrawOpKind::fill_rounded_rect,
                .rect = bounds,
                .color = color,
                .corner_radius = radius};
}

[[nodiscard]] DrawOp make_fill_ellipse(Rect2f bounds, Rgba8 color) {
  return DrawOp{
      .kind = DrawOpKind::fill_ellipse, .rect = bounds, .color = color};
}

[[nodiscard]] DiagramPoint rotate_around(DiagramPoint p, DiagramPoint c,
                                         float rad) noexcept {
  const float s = std::sin(rad);
  const float cosv = std::cos(rad);
  const float dx = p.x - c.x;
  const float dy = p.y - c.y;
  return DiagramPoint{.x = c.x + dx * cosv - dy * s,
                      .y = c.y + dx * s + dy * cosv};
}

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_node(const DiagramNode &n) {
  if (n.id.empty()) {
    return std::unexpected(
        make_invalid("diagram.empty_id", "diagram node id must be non-empty"));
  }
  if (!finite_point(n.cx, n.cy) || !std::isfinite(n.rotation_rad)) {
    return std::unexpected(make_invalid("diagram.bad_node",
                                        "node center/rotation must be finite"));
  }
  if (!std::isfinite(n.width) || !(n.width > 0.0f) ||
      !std::isfinite(n.height) || !(n.height > 0.0f)) {
    return std::unexpected(
        make_invalid("diagram.bad_node", "node size must be finite and > 0"));
  }
  if (!std::isfinite(n.reveal)) {
    return std::unexpected(
        make_invalid("diagram.bad_reveal", "node reveal must be finite"));
  }
  return {};
}

[[nodiscard]] DiagramPoint lerp(DiagramPoint a, DiagramPoint b,
                                float t) noexcept {
  return DiagramPoint{.x = a.x + (b.x - a.x) * t, .y = a.y + (b.y - a.y) * t};
}

[[nodiscard]] float length(DiagramPoint a, DiagramPoint b) noexcept {
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  return std::sqrt(dx * dx + dy * dy);
}

void append_arrow_head(std::vector<DrawOp> &ops, DiagramPoint from,
                       DiagramPoint to, float head_length, float head_width,
                       float stroke_width, Rgba8 color) {
  const float len = length(from, to);
  if (!(len > 1.0e-3f) || head_length >= len) {
    return;
  }
  const float ux = (to.x - from.x) / len;
  const float uy = (to.y - from.y) / len;
  const float px = -uy;
  const float py = ux;
  const float base_x = to.x - ux * head_length;
  const float base_y = to.y - uy * head_length;
  const float half = head_width * 0.5f;
  ops.push_back(make_line(from.x, from.y, base_x, base_y, stroke_width, color));
  ops.push_back(make_line(to.x, to.y, base_x + px * half, base_y + py * half,
                          stroke_width, color));
  ops.push_back(make_line(to.x, to.y, base_x - px * half, base_y - py * half,
                          stroke_width, color));
}

void append_revealed_segment(std::vector<DrawOp> &ops, DiagramPoint a,
                             DiagramPoint b, float reveal, float width,
                             Rgba8 color) {
  const float t = clamp01(reveal);
  if (t <= 0.0f) {
    return;
  }
  const DiagramPoint end = lerp(a, b, t);
  ops.push_back(make_line(a.x, a.y, end.x, end.y, width, color));
}

} // namespace

std::expected<DiagramPoint, diagnostics::Error>
diagram_port_point(const DiagramNode &node, DiagramPort port) {
  if (auto ok = validate_node(node); !ok) {
    return std::unexpected(ok.error());
  }
  const float hx = node.width * 0.5f;
  const float hy = node.height * 0.5f;
  DiagramPoint local{.x = node.cx, .y = node.cy};
  switch (port) {
  case DiagramPort::center:
    break;
  case DiagramPort::north:
    local.y = node.cy - hy;
    break;
  case DiagramPort::east:
    local.x = node.cx + hx;
    break;
  case DiagramPort::south:
    local.y = node.cy + hy;
    break;
  case DiagramPort::west:
    local.x = node.cx - hx;
    break;
  }
  if (node.rotation_rad == 0.0f) {
    return local;
  }
  return rotate_around(local, DiagramPoint{node.cx, node.cy},
                       node.rotation_rad);
}

std::expected<DiagramLayout, diagnostics::Error>
layout_diagram(const DiagramScene &scene) {
  if (scene.width_px == 0 || scene.height_px == 0) {
    return std::unexpected(
        make_invalid("diagram.bad_scene", "diagram scene size must be > 0"));
  }

  std::unordered_map<std::string, const DiagramNode *> by_id;
  by_id.reserve(scene.nodes.size());
  for (const auto &n : scene.nodes) {
    if (auto ok = validate_node(n); !ok) {
      return std::unexpected(ok.error());
    }
    if (!by_id.emplace(n.id, &n).second) {
      return std::unexpected(
          make_invalid("diagram.duplicate_id", "duplicate diagram node id"));
    }
  }

  DiagramLayout layout;
  layout.ops.push_back(
      DrawOp{.kind = DrawOpKind::clear, .color = scene.clear_color});

  // Guides under nodes.
  for (const auto &g : scene.guides) {
    if (g.id.empty() || !std::isfinite(g.position) ||
        !std::isfinite(g.origin) || !std::isfinite(g.length) ||
        !(g.length > 0.0f)) {
      return std::unexpected(
          make_invalid("diagram.bad_guide", "guide fields invalid"));
    }
    if (g.horizontal) {
      layout.ops.push_back(make_line(g.origin, g.position, g.origin + g.length,
                                     g.position, g.stroke_width, g.color));
    } else {
      layout.ops.push_back(make_line(g.position, g.origin, g.position,
                                     g.origin + g.length, g.stroke_width,
                                     g.color));
    }
  }

  for (const auto &n : scene.nodes) {
    const float reveal = clamp01(n.reveal);
    if (reveal <= 0.0f) {
      continue;
    }
    const Rgba8 fill = scale_alpha(n.fill, reveal);
    const Rgba8 stroke = scale_alpha(n.stroke, reveal);
    // Axis-aligned bounds at the unrotated AABB (rotation affects ports/edges;
    // node body uses AABB for DrawOp kinds available today).
    Rect2f bounds{.x = n.cx - n.width * 0.5f,
                  .y = n.cy - n.height * 0.5f,
                  .width = n.width,
                  .height = n.height};
    if (n.shape == DiagramNodeShape::ellipse) {
      layout.ops.push_back(make_fill_ellipse(bounds, fill));
    } else {
      layout.ops.push_back(make_fill_rounded(bounds, n.corner_radius, fill));
    }
    // Stroke as four edges (shared primitives have no stroke_rect).
    const float x0 = bounds.x;
    const float y0 = bounds.y;
    const float x1 = bounds.x + bounds.width;
    const float y1 = bounds.y + bounds.height;
    layout.ops.push_back(make_line(x0, y0, x1, y0, n.stroke_width, stroke));
    layout.ops.push_back(make_line(x1, y0, x1, y1, n.stroke_width, stroke));
    layout.ops.push_back(make_line(x1, y1, x0, y1, n.stroke_width, stroke));
    layout.ops.push_back(make_line(x0, y1, x0, y0, n.stroke_width, stroke));
  }

  for (const auto &e : scene.edges) {
    if (e.id.empty() || e.from_id.empty() || e.to_id.empty()) {
      return std::unexpected(
          make_invalid("diagram.bad_edge", "edge ids must be non-empty"));
    }
    if (!std::isfinite(e.reveal) || !std::isfinite(e.stroke_width) ||
        !(e.stroke_width > 0.0f)) {
      return std::unexpected(
          make_invalid("diagram.bad_edge", "edge stroke/reveal invalid"));
    }
    const auto from_n = by_id.find(e.from_id);
    const auto to_n = by_id.find(e.to_id);
    if (from_n == by_id.end() || to_n == by_id.end()) {
      return std::unexpected(make_invalid("diagram.missing_node",
                                          "edge references unknown node id"));
    }
    auto from = diagram_port_point(*from_n->second, e.from_port);
    if (!from) {
      return std::unexpected(from.error());
    }
    auto to = diagram_port_point(*to_n->second, e.to_port);
    if (!to) {
      return std::unexpected(to.error());
    }

    DiagramResolvedEdge resolved;
    resolved.id = e.id;
    resolved.from = *from;
    resolved.to = *to;
    resolved.reveal = clamp01(e.reveal);

    const Rgba8 color = scale_alpha(e.color, 1.0f);
    if (resolved.reveal <= 0.0f) {
      layout.edges.push_back(std::move(resolved));
      continue;
    }

    if (e.kind == DiagramEdgeKind::orthogonal) {
      resolved.mid = DiagramPoint{.x = to->x, .y = from->y};
      resolved.has_mid = true;
      const float len1 = length(*from, resolved.mid);
      const float len2 = length(resolved.mid, *to);
      const float total = len1 + len2;
      const float travel = total * resolved.reveal;
      if (travel <= len1 || !(len2 > 0.0f)) {
        const float t = len1 > 0.0f ? travel / len1 : 1.0f;
        const DiagramPoint end = lerp(*from, resolved.mid, clamp01(t));
        layout.ops.push_back(
            make_line(from->x, from->y, end.x, end.y, e.stroke_width, color));
      } else {
        layout.ops.push_back(make_line(from->x, from->y, resolved.mid.x,
                                       resolved.mid.y, e.stroke_width, color));
        const float t = (travel - len1) / len2;
        const DiagramPoint end = lerp(resolved.mid, *to, clamp01(t));
        layout.ops.push_back(make_line(resolved.mid.x, resolved.mid.y, end.x,
                                       end.y, e.stroke_width, color));
      }
    } else if (e.kind == DiagramEdgeKind::arrow) {
      if (resolved.reveal >= 1.0f) {
        append_arrow_head(layout.ops, *from, *to, e.head_length, e.head_width,
                          e.stroke_width, color);
      } else {
        append_revealed_segment(layout.ops, *from, *to, resolved.reveal,
                                e.stroke_width, color);
      }
    } else {
      append_revealed_segment(layout.ops, *from, *to, resolved.reveal,
                              e.stroke_width, color);
    }
    layout.edges.push_back(std::move(resolved));
  }

  for (const auto &c : scene.callouts) {
    if (c.id.empty() || c.target_id.empty()) {
      return std::unexpected(
          make_invalid("diagram.bad_callout", "callout ids must be non-empty"));
    }
    const auto it = by_id.find(c.target_id);
    if (it == by_id.end()) {
      return std::unexpected(make_invalid(
          "diagram.missing_node", "callout references unknown node id"));
    }
    auto tip = diagram_port_point(*it->second, c.target_port);
    if (!tip) {
      return std::unexpected(tip.error());
    }
    const float reveal = clamp01(c.reveal);
    if (reveal <= 0.0f) {
      continue;
    }
    Rect2f box{.x = c.box_cx - c.box_w * 0.5f,
               .y = c.box_cy - c.box_h * 0.5f,
               .width = c.box_w,
               .height = c.box_h};
    layout.ops.push_back(
        make_fill_rounded(box, 3.0f, scale_alpha(c.fill, reveal)));
    layout.ops.push_back(make_line(tip->x, tip->y, c.box_cx, c.box_cy, 1.5f,
                                   scale_alpha(c.stroke, reveal)));
  }

  for (const auto &lab : scene.labels) {
    if (lab.id.empty()) {
      return std::unexpected(
          make_invalid("diagram.bad_label", "label id must be non-empty"));
    }
    float lx = lab.x;
    float ly = lab.y;
    if (!lab.node_id.empty()) {
      const auto it = by_id.find(lab.node_id);
      if (it == by_id.end()) {
        return std::unexpected(make_invalid(
            "diagram.missing_node", "label references unknown node id"));
      }
      const auto &n = *it->second;
      DiagramPoint p = rotate_around(DiagramPoint{n.cx + lab.x, n.cy + lab.y},
                                     DiagramPoint{n.cx, n.cy}, n.rotation_rad);
      lx = p.x;
      ly = p.y;
    }
    const float reveal = clamp01(lab.reveal);
    if (reveal <= 0.0f) {
      continue;
    }
    layout.ops.push_back(make_fill_rect(Rect2f{.x = lx - lab.width * 0.5f,
                                               .y = ly - lab.height * 0.5f,
                                               .width = lab.width,
                                               .height = lab.height},
                                        scale_alpha(lab.color, reveal)));
  }

  return layout;
}

std::string format_diagram_layout_golden(std::string_view label,
                                         const DiagramScene &scene,
                                         const DiagramLayout &layout) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3);
  oss << "diagram " << label << '\n';
  oss << "size " << scene.width_px << "x" << scene.height_px << '\n';
  oss << "nodes " << scene.nodes.size() << '\n';
  for (const auto &n : scene.nodes) {
    oss << "  node " << n.id << " c=" << n.cx << "," << n.cy
        << " wh=" << n.width << "x" << n.height << " rot=" << n.rotation_rad
        << " reveal=" << n.reveal << '\n';
    for (auto port : {DiagramPort::west, DiagramPort::east, DiagramPort::north,
                      DiagramPort::south}) {
      auto p = diagram_port_point(n, port);
      if (p) {
        oss << "    port " << diagram_port_id(port) << "=" << p->x << ","
            << p->y << '\n';
      }
    }
  }
  oss << "edges " << layout.edges.size() << '\n';
  for (const auto &e : layout.edges) {
    oss << "  edge " << e.id << " from=" << e.from.x << "," << e.from.y
        << " to=" << e.to.x << "," << e.to.y << " reveal=" << e.reveal;
    if (e.has_mid) {
      oss << " mid=" << e.mid.x << "," << e.mid.y;
    }
    oss << '\n';
  }
  oss << "ops " << layout.ops.size() << '\n';
  for (const auto &op : layout.ops) {
    oss << "  kind=" << static_cast<int>(op.kind);
    if (op.kind == DrawOpKind::stroke_line) {
      oss << " line=" << op.rect.x << "," << op.rect.y << "->" << op.x1 << ","
          << op.y1 << " w=" << op.stroke_width;
    } else if (op.kind != DrawOpKind::clear) {
      oss << " rect=" << op.rect.x << "," << op.rect.y << "," << op.rect.width
          << "x" << op.rect.height;
    }
    oss << " a=" << static_cast<int>(op.color.a) << '\n';
  }
  return oss.str();
}

} // namespace instantee::render_core
