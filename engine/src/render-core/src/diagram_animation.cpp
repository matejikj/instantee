#include <instantee/render_core/diagram_animation.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace instantee::render_core {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "diagram_animation", message};
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

[[nodiscard]] float lerp(float a, float b, float t) noexcept {
  return a + (b - a) * t;
}

[[nodiscard]] bool in_window(float t, float start, float end) noexcept {
  return t >= start && t <= end;
}

[[nodiscard]] float stage_reveal(float t, float start, float end) noexcept {
  if (t <= start) {
    return 0.0f;
  }
  if (t >= end) {
    return 1.0f;
  }
  const float span = end - start;
  if (!(span > 0.0f)) {
    return 1.0f;
  }
  return clamp01((t - start) / span);
}

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_spec(const DiagramAnimationSpec &spec) {
  if (spec.base.width_px == 0 || spec.base.height_px == 0) {
    return std::unexpected(
        make_invalid("diagram_anim.bad_scene", "base scene size must be > 0"));
  }
  std::unordered_set<std::string> node_ids;
  node_ids.reserve(spec.base.nodes.size());
  for (const auto &n : spec.base.nodes) {
    if (n.id.empty()) {
      return std::unexpected(
          make_invalid("diagram_anim.empty_id", "node id must be non-empty"));
    }
    if (!node_ids.insert(n.id).second) {
      return std::unexpected(
          make_invalid("diagram_anim.duplicate_id", "duplicate base node id"));
    }
  }
  std::unordered_set<std::string> edge_ids;
  edge_ids.reserve(spec.base.edges.size());
  for (const auto &e : spec.base.edges) {
    if (e.id.empty()) {
      return std::unexpected(
          make_invalid("diagram_anim.empty_id", "edge id must be non-empty"));
    }
    if (!edge_ids.insert(e.id).second) {
      return std::unexpected(
          make_invalid("diagram_anim.duplicate_id", "duplicate base edge id"));
    }
    if (!node_ids.contains(e.from_id) || !node_ids.contains(e.to_id)) {
      return std::unexpected(make_invalid("diagram_anim.missing_node",
                                          "edge references unknown node"));
    }
  }

  std::unordered_set<std::string> staged_edges;
  for (const auto &stage : spec.reveal_stages) {
    if (!std::isfinite(stage.start_t) || !std::isfinite(stage.end_t) ||
        stage.end_t < stage.start_t) {
      return std::unexpected(make_invalid("diagram_anim.bad_stage",
                                          "reveal stage window invalid"));
    }
    for (const auto &id : stage.edge_ids) {
      if (!edge_ids.contains(id)) {
        return std::unexpected(make_invalid("diagram_anim.missing_edge",
                                            "reveal stage unknown edge id"));
      }
      if (!staged_edges.insert(id).second) {
        return std::unexpected(
            make_invalid("diagram_anim.duplicate_stage",
                         "edge appears in more than one reveal stage"));
      }
    }
  }

  float prev_t = -1.0f;
  for (const auto &pose : spec.layout_poses) {
    if (!std::isfinite(pose.t) || pose.t < 0.0f || pose.t > 1.0f) {
      return std::unexpected(make_invalid(
          "diagram_anim.bad_pose", "layout pose t must be finite in [0,1]"));
    }
    if (pose.t < prev_t) {
      return std::unexpected(make_invalid("diagram_anim.unsorted_poses",
                                          "layout poses must be sorted by t"));
    }
    prev_t = pose.t;
    for (const auto &n : pose.nodes) {
      if (!node_ids.contains(n.id)) {
        return std::unexpected(make_invalid("diagram_anim.missing_node",
                                            "layout pose unknown node id"));
      }
      if (!std::isfinite(n.cx) || !std::isfinite(n.cy) ||
          !std::isfinite(n.rotation_rad)) {
        return std::unexpected(make_invalid(
            "diagram_anim.bad_pose", "layout pose values must be finite"));
      }
    }
  }

  auto check_ids = [&](std::span<const std::string> ids,
                       bool edges) -> std::expected<void, diagnostics::Error> {
    for (const auto &id : ids) {
      if (edges) {
        if (!edge_ids.contains(id)) {
          return std::unexpected(make_invalid("diagram_anim.missing_edge",
                                              "highlight unknown edge id"));
        }
      } else if (!node_ids.contains(id)) {
        return std::unexpected(make_invalid("diagram_anim.missing_node",
                                            "highlight unknown node id"));
      }
    }
    return {};
  };

  if (auto ok = check_ids(spec.active_path.edge_ids, true); !ok) {
    return ok;
  }
  if (auto ok = check_ids(spec.active_path.node_ids, false); !ok) {
    return ok;
  }
  if (auto ok = check_ids(spec.group_compare.group_a_node_ids, false); !ok) {
    return ok;
  }
  if (auto ok = check_ids(spec.group_compare.group_b_node_ids, false); !ok) {
    return ok;
  }
  if (!std::isfinite(spec.active_path.start_t) ||
      !std::isfinite(spec.active_path.end_t) ||
      spec.active_path.end_t < spec.active_path.start_t ||
      !std::isfinite(spec.active_path.stroke_width_scale) ||
      !(spec.active_path.stroke_width_scale > 0.0f)) {
    return std::unexpected(make_invalid("diagram_anim.bad_path",
                                        "active path window/scale invalid"));
  }
  if (!std::isfinite(spec.group_compare.start_t) ||
      !std::isfinite(spec.group_compare.end_t) ||
      spec.group_compare.end_t < spec.group_compare.start_t) {
    return std::unexpected(make_invalid("diagram_anim.bad_compare",
                                        "group compare window invalid"));
  }
  // Disjoint group membership.
  std::unordered_set<std::string> seen;
  for (const auto &id : spec.group_compare.group_a_node_ids) {
    seen.insert(id);
  }
  for (const auto &id : spec.group_compare.group_b_node_ids) {
    if (!seen.insert(id).second) {
      return std::unexpected(
          make_invalid("diagram_anim.overlap_groups",
                       "group compare node cannot be in both groups"));
    }
  }
  return {};
}

struct PoseSample {
  float cx = 0;
  float cy = 0;
  float rotation_rad = 0;
};

[[nodiscard]] PoseSample
sample_node_pose(const DiagramNode &base,
                 const std::vector<DiagramLayoutPose> &poses,
                 float t) noexcept {
  PoseSample out{
      .cx = base.cx, .cy = base.cy, .rotation_rad = base.rotation_rad};
  if (poses.empty()) {
    return out;
  }

  // Collect keyframes that mention this node; fall back to base at t=0/1 if
  // the node is absent from the first/last pose.
  struct Key {
    float t;
    PoseSample p;
  };
  std::vector<Key> keys;
  keys.reserve(poses.size() + 2);
  bool first_hit = false;
  for (const auto &pose : poses) {
    for (const auto &n : pose.nodes) {
      if (n.id == base.id) {
        if (!first_hit && pose.t > 0.0f) {
          keys.push_back(Key{0.0f, out});
        }
        keys.push_back(Key{pose.t, PoseSample{n.cx, n.cy, n.rotation_rad}});
        first_hit = true;
        break;
      }
    }
  }
  if (keys.empty()) {
    return out;
  }
  if (keys.back().t < 1.0f) {
    keys.push_back(Key{1.0f, keys.back().p});
  }

  if (t <= keys.front().t) {
    return keys.front().p;
  }
  if (t >= keys.back().t) {
    return keys.back().p;
  }
  for (std::size_t i = 1; i < keys.size(); ++i) {
    if (t <= keys[i].t) {
      const auto &a = keys[i - 1];
      const auto &b = keys[i];
      const float span = b.t - a.t;
      const float u = span > 0.0f ? (t - a.t) / span : 1.0f;
      return PoseSample{.cx = lerp(a.p.cx, b.p.cx, u),
                        .cy = lerp(a.p.cy, b.p.cy, u),
                        .rotation_rad =
                            lerp(a.p.rotation_rad, b.p.rotation_rad, u)};
    }
  }
  return keys.back().p;
}

} // namespace

std::expected<DiagramAnimationSample, diagnostics::Error>
sample_diagram_animation(const DiagramAnimationSpec &spec, float t) {
  if (!std::isfinite(t)) {
    return std::unexpected(
        make_invalid("diagram_anim.bad_t", "sample t must be finite"));
  }
  if (auto ok = validate_spec(spec); !ok) {
    return std::unexpected(ok.error());
  }
  const float ut = clamp01(t);

  DiagramAnimationSample sample;
  sample.t = ut;
  sample.scene = spec.base;

  // Build reveal map from stages.
  std::unordered_map<std::string, float> edge_reveal;
  for (const auto &stage : spec.reveal_stages) {
    const float r = stage_reveal(ut, stage.start_t, stage.end_t);
    for (const auto &id : stage.edge_ids) {
      edge_reveal[id] = r;
    }
  }
  for (auto &e : sample.scene.edges) {
    if (auto it = edge_reveal.find(e.id); it != edge_reveal.end()) {
      e.reveal = it->second;
    }
  }

  // Transform graph layout.
  for (auto &n : sample.scene.nodes) {
    const auto pose = sample_node_pose(n, spec.layout_poses, ut);
    n.cx = pose.cx;
    n.cy = pose.cy;
    n.rotation_rad = pose.rotation_rad;
  }

  // Group compare colors (applied before path so path can override).
  const bool compare_on =
      (!spec.group_compare.group_a_node_ids.empty() ||
       !spec.group_compare.group_b_node_ids.empty()) &&
      in_window(ut, spec.group_compare.start_t, spec.group_compare.end_t);
  sample.compare_active = compare_on;
  if (compare_on) {
    std::unordered_set<std::string> a(
        spec.group_compare.group_a_node_ids.begin(),
        spec.group_compare.group_a_node_ids.end());
    std::unordered_set<std::string> b(
        spec.group_compare.group_b_node_ids.begin(),
        spec.group_compare.group_b_node_ids.end());
    for (auto &n : sample.scene.nodes) {
      if (a.contains(n.id)) {
        n.fill = spec.group_compare.group_a_fill;
      } else if (b.contains(n.id)) {
        n.fill = spec.group_compare.group_b_fill;
      } else {
        n.fill = spec.group_compare.dim_fill;
      }
    }
  }

  // Active path highlight.
  const bool path_on =
      (!spec.active_path.edge_ids.empty() ||
       !spec.active_path.node_ids.empty()) &&
      in_window(ut, spec.active_path.start_t, spec.active_path.end_t);
  sample.path_active = path_on;
  if (path_on) {
    std::unordered_set<std::string> pe(spec.active_path.edge_ids.begin(),
                                       spec.active_path.edge_ids.end());
    std::unordered_set<std::string> pn(spec.active_path.node_ids.begin(),
                                       spec.active_path.node_ids.end());
    for (auto &e : sample.scene.edges) {
      if (pe.contains(e.id)) {
        e.color = spec.active_path.color;
        e.stroke_width *= spec.active_path.stroke_width_scale;
      }
    }
    for (auto &n : sample.scene.nodes) {
      if (pn.contains(n.id)) {
        n.stroke = spec.active_path.color;
        n.stroke_width *= spec.active_path.stroke_width_scale;
      }
    }
  }

  return sample;
}

std::expected<std::string, diagnostics::Error>
format_diagram_animation_demo(const DiagramAnimationSpec &spec,
                              std::span<const float> times) {
  if (times.empty()) {
    return std::unexpected(
        make_invalid("diagram_anim.empty_demo", "demo needs at least one t"));
  }
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3);
  oss << "diagram-animation-demo frames=" << times.size() << '\n';
  oss << "nodes=" << spec.base.nodes.size()
      << " edges=" << spec.base.edges.size()
      << " stages=" << spec.reveal_stages.size()
      << " poses=" << spec.layout_poses.size() << '\n';

  for (float t : times) {
    auto sample = sample_diagram_animation(spec, t);
    if (!sample) {
      return std::unexpected(sample.error());
    }
    auto layout = layout_diagram(sample->scene);
    if (!layout) {
      return std::unexpected(layout.error());
    }
    oss << "frame t=" << sample->t << " path=" << (sample->path_active ? 1 : 0)
        << " compare=" << (sample->compare_active ? 1 : 0)
        << " ops=" << layout->ops.size() << '\n';
    for (const auto &n : sample->scene.nodes) {
      oss << "  node " << n.id << " c=" << n.cx << "," << n.cy
          << " rot=" << n.rotation_rad << " reveal=" << n.reveal
          << " fill_a=" << static_cast<int>(n.fill.a)
          << " stroke_rgb=" << static_cast<int>(n.stroke.r) << ","
          << static_cast<int>(n.stroke.g) << "," << static_cast<int>(n.stroke.b)
          << '\n';
    }
    for (const auto &e : sample->scene.edges) {
      oss << "  edge " << e.id << " reveal=" << e.reveal
          << " w=" << e.stroke_width << " rgb=" << static_cast<int>(e.color.r)
          << "," << static_cast<int>(e.color.g) << ","
          << static_cast<int>(e.color.b) << '\n';
    }
    for (const auto &re : layout->edges) {
      oss << "  resolved " << re.id << " from=" << re.from.x << "," << re.from.y
          << " to=" << re.to.x << "," << re.to.y << " reveal=" << re.reveal
          << '\n';
    }
  }
  return oss.str();
}

} // namespace instantee::render_core
