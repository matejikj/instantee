#include <instantee/render_core/paper_fold.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace instantee::render_core {
namespace {

diagnostics::Error make_err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "paper_fold", message};
}

[[nodiscard]] bool finite_vec(const core::Vec3 &v) noexcept {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] double length2(const core::Vec3 &v) noexcept {
  return v.x * v.x + v.y * v.y + v.z * v.z;
}

[[nodiscard]] core::Vec3 normalize_xy(const core::Vec3 &v) {
  const double len = std::sqrt(v.x * v.x + v.y * v.y);
  if (!(len > 1e-12)) {
    return core::Vec3{1, 0, 0};
  }
  return core::Vec3{v.x / len, v.y / len, 0};
}

[[nodiscard]] core::Vec3 cross(const core::Vec3 &a,
                               const core::Vec3 &b) noexcept {
  return core::Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                    a.x * b.y - a.y * b.x};
}

[[nodiscard]] double dot(const core::Vec3 &a, const core::Vec3 &b) noexcept {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Rodrigues rotation of v around unit axis by angle radians.
[[nodiscard]] core::Vec3 rotate_around(const core::Vec3 &v,
                                       const core::Vec3 &axis,
                                       double angle) noexcept {
  const double c = std::cos(angle);
  const double s = std::sin(angle);
  const core::Vec3 kxv = cross(axis, v);
  const double kdv = dot(axis, v);
  return core::Vec3{v.x * c + kxv.x * s + axis.x * kdv * (1.0 - c),
                    v.y * c + kxv.y * s + axis.y * kdv * (1.0 - c),
                    v.z * c + kxv.z * s + axis.z * kdv * (1.0 - c)};
}

[[nodiscard]] core::Vec3 project_on_crease(const core::Vec3 &p,
                                           const PaperFoldCrease &crease) {
  const core::Vec3 d = normalize_xy(crease.direction);
  const core::Vec3 rel{p.x - crease.origin.x, p.y - crease.origin.y,
                       p.z - crease.origin.z};
  const double t = rel.x * d.x + rel.y * d.y;
  return core::Vec3{crease.origin.x + d.x * t, crease.origin.y + d.y * t,
                    crease.origin.z};
}

} // namespace

std::expected<void, diagnostics::Error>
validate_paper_fold_mesh(const PaperFoldMesh &mesh) {
  if (mesh.id.empty()) {
    return std::unexpected(make_err("invalid_id", "Fold mesh id is empty"));
  }
  if (mesh.bind_vertices.size() < 3U || mesh.indices.size() < 3U ||
      mesh.indices.size() % 3U != 0U) {
    return std::unexpected(
        make_err("invalid_topology", "Fold mesh needs triangles"));
  }
  if (mesh.segments.empty()) {
    return std::unexpected(
        make_err("invalid_segments", "Fold mesh needs at least one segment"));
  }
  for (const auto &v : mesh.bind_vertices) {
    if (!finite_vec(v)) {
      return std::unexpected(
          make_err("non_finite", "Bind vertices must be finite"));
    }
  }
  for (std::uint32_t idx : mesh.indices) {
    if (static_cast<std::size_t>(idx) >= mesh.bind_vertices.size()) {
      return std::unexpected(
          make_err("bad_index", "Triangle index out of range"));
    }
  }

  std::unordered_set<std::string> crease_ids;
  for (const auto &crease : mesh.creases) {
    if (crease.id.empty() || !crease_ids.insert(crease.id).second) {
      return std::unexpected(make_err(
          "invalid_crease", "Crease ids must be unique and non-empty"));
    }
    if (!finite_vec(crease.origin) || !finite_vec(crease.direction)) {
      return std::unexpected(
          make_err("non_finite", "Crease geometry must be finite"));
    }
    if (!(length2(core::Vec3{crease.direction.x, crease.direction.y, 0}) >
          1e-12)) {
      return std::unexpected(
          make_err("invalid_crease", "Crease direction XY must be non-zero"));
    }
  }

  std::unordered_set<std::string> segment_ids;
  std::vector<int> owner(mesh.bind_vertices.size(), -1);
  bool has_base = false;
  for (std::size_t si = 0; si < mesh.segments.size(); ++si) {
    const auto &seg = mesh.segments[si];
    if (seg.id.empty() || !segment_ids.insert(seg.id).second) {
      return std::unexpected(make_err(
          "invalid_segment", "Segment ids must be unique and non-empty"));
    }
    if (seg.crease_id.empty()) {
      has_base = true;
    } else if (!crease_ids.contains(seg.crease_id)) {
      return std::unexpected(
          make_err("unknown_crease", "Segment references unknown crease"));
    }
    if (seg.vertex_indices.empty()) {
      return std::unexpected(
          make_err("empty_segment", "Segment has no vertices"));
    }
    for (std::uint32_t vi : seg.vertex_indices) {
      if (static_cast<std::size_t>(vi) >= mesh.bind_vertices.size()) {
        return std::unexpected(
            make_err("bad_index", "Segment vertex out of range"));
      }
      if (owner[vi] >= 0) {
        return std::unexpected(make_err(
            "shared_vertex", "Vertex belongs to more than one fold segment"));
      }
      owner[vi] = static_cast<int>(si);
    }
  }
  if (!has_base) {
    return std::unexpected(
        make_err("missing_base", "Fold mesh needs a fixed base segment"));
  }
  for (int o : owner) {
    if (o < 0) {
      return std::unexpected(make_err(
          "unowned_vertex", "Every bind vertex must belong to a segment"));
    }
  }
  return {};
}

std::expected<void, diagnostics::Error>
validate_paper_fold_clip(const PaperFoldClip &clip, const PaperFoldMesh &mesh) {
  if (auto ok = validate_paper_fold_mesh(mesh); !ok) {
    return ok;
  }
  if (clip.id.empty()) {
    return std::unexpected(make_err("invalid_id", "Fold clip id is empty"));
  }
  if (clip.keys.size() < 2U) {
    return std::unexpected(
        make_err("invalid_keys", "Fold clip needs at least two keyframes"));
  }
  std::unordered_set<std::string> crease_ids;
  for (const auto &c : mesh.creases) {
    crease_ids.insert(c.id);
  }
  core::FrameIndex prev{};
  bool first = true;
  for (const auto &key : clip.keys) {
    if (!first && key.frame < prev) {
      return std::unexpected(
          make_err("invalid_time", "Fold keyframes must be non-decreasing"));
    }
    first = false;
    prev = key.frame;
    if (key.crease_angles.empty()) {
      return std::unexpected(
          make_err("empty_angles", "Fold keyframe has no crease angles"));
    }
    for (const auto &[cid, angle] : key.crease_angles) {
      if (!crease_ids.contains(cid)) {
        return std::unexpected(
            make_err("unknown_crease", "Keyframe references unknown crease"));
      }
      if (!std::isfinite(angle)) {
        return std::unexpected(
            make_err("non_finite", "Fold angles must be finite"));
      }
    }
  }
  return {};
}

PaperFoldMesh make_letter_fold_mesh(double width, double height) {
  PaperFoldMesh mesh;
  mesh.id = "letter-fold";
  const double w = width;
  const double h = height;
  const double mid = w * 0.5;
  // 0--1--2
  // | /| /|
  // 3--4--5
  mesh.bind_vertices = {
      core::Vec3{0, 0, 0}, core::Vec3{mid, 0, 0}, core::Vec3{w, 0, 0},
      core::Vec3{0, h, 0}, core::Vec3{mid, h, 0}, core::Vec3{w, h, 0},
  };
  mesh.indices = {
      0, 1, 4, 0, 4, 3, // left (base)
      1, 2, 5, 1, 5, 4, // right (hinged)
  };
  mesh.creases = {PaperFoldCrease{
      .id = "hinge",
      .origin = core::Vec3{mid, 0, 0},
      .direction = core::Vec3{0, 1, 0},
  }};
  mesh.segments = {
      PaperFoldSegment{.id = "panel_left",
                       .crease_id = "",
                       .vertex_indices = {0, 3},
                       .occlusion_order = 0},
      // Crease column shared by topology but owned by hinged panel for motion;
      // left panel keeps outer column only. Mid vertices hinge with the right.
      PaperFoldSegment{.id = "panel_right",
                       .crease_id = "hinge",
                       .vertex_indices = {1, 2, 4, 5},
                       .occlusion_order = 1},
  };
  return mesh;
}

PaperFoldClip make_letter_fold_clip(std::uint64_t fold_frames,
                                    std::uint64_t hold_frames) {
  const std::uint64_t fold = fold_frames == 0 ? 1 : fold_frames;
  const std::uint64_t hold = hold_frames;
  const std::uint64_t t_fold = fold;
  const std::uint64_t t_hold = fold + hold;
  const std::uint64_t t_open = fold + hold + fold;
  PaperFoldClip clip;
  clip.id = "letter-fold-open-close";
  clip.keys = {
      PaperFoldKeyframe{.frame = core::FrameIndex{0},
                        .crease_angles = {{"hinge", 0.0}}},
      PaperFoldKeyframe{.frame = core::FrameIndex{t_fold},
                        .crease_angles = {{"hinge", 3.141592653589793}}},
      PaperFoldKeyframe{.frame = core::FrameIndex{t_hold},
                        .crease_angles = {{"hinge", 3.141592653589793}}},
      PaperFoldKeyframe{.frame = core::FrameIndex{t_open},
                        .crease_angles = {{"hinge", 0.0}}},
  };
  return clip;
}

std::expected<std::unordered_map<std::string, double>, diagnostics::Error>
sample_paper_fold_angles(const PaperFoldClip &clip, const PaperFoldMesh &mesh,
                         core::FrameIndex frame) {
  if (auto ok = validate_paper_fold_clip(clip, mesh); !ok) {
    return std::unexpected(ok.error());
  }

  const auto &keys = clip.keys;
  if (frame <= keys.front().frame) {
    return keys.front().crease_angles;
  }
  if (frame >= keys.back().frame) {
    return keys.back().crease_angles;
  }

  std::size_t upper = 1;
  while (upper + 1 < keys.size() && keys[upper].frame < frame) {
    ++upper;
  }
  const auto &a = keys[upper - 1];
  const auto &b = keys[upper];
  const auto fa = a.frame.value();
  const auto fb = b.frame.value();
  const auto f = frame.value();
  double u = 0.0;
  if (fb > fa) {
    u = static_cast<double>(f - fa) / static_cast<double>(fb - fa);
  }

  std::unordered_map<std::string, double> out;
  std::unordered_set<std::string> ids;
  for (const auto &[id, _] : a.crease_angles) {
    ids.insert(id);
  }
  for (const auto &[id, _] : b.crease_angles) {
    ids.insert(id);
  }
  for (const auto &id : ids) {
    const double va =
        a.crease_angles.contains(id) ? a.crease_angles.at(id) : 0.0;
    const double vb =
        b.crease_angles.contains(id) ? b.crease_angles.at(id) : 0.0;
    out.emplace(id, va + (vb - va) * u);
  }
  return out;
}

std::expected<PaperFoldPose, diagnostics::Error> evaluate_paper_fold(
    const PaperFoldMesh &mesh,
    const std::unordered_map<std::string, double> &crease_angles) {
  if (auto ok = validate_paper_fold_mesh(mesh); !ok) {
    return std::unexpected(ok.error());
  }
  for (const auto &[cid, angle] : crease_angles) {
    bool found = false;
    for (const auto &c : mesh.creases) {
      if (c.id == cid) {
        found = true;
        break;
      }
    }
    if (!found) {
      return std::unexpected(
          make_err("unknown_crease", "Fold angle references unknown crease"));
    }
    if (!std::isfinite(angle)) {
      return std::unexpected(
          make_err("non_finite", "Fold angles must be finite"));
    }
  }

  std::unordered_map<std::string, const PaperFoldCrease *> crease_by_id;
  for (const auto &c : mesh.creases) {
    crease_by_id.emplace(c.id, &c);
  }

  PaperFoldPose pose;
  pose.vertices = mesh.bind_vertices;
  pose.indices = mesh.indices;
  pose.draw_order.reserve(mesh.segments.size());

  for (const auto &seg : mesh.segments) {
    double angle = 0.0;
    if (!seg.crease_id.empty()) {
      if (const auto it = crease_angles.find(seg.crease_id);
          it != crease_angles.end()) {
        angle = it->second;
      }
    }

    if (!seg.crease_id.empty() && std::fabs(angle) > 1e-15) {
      const auto *crease = crease_by_id.at(seg.crease_id);
      const core::Vec3 axis = normalize_xy(crease->direction);
      for (std::uint32_t vi : seg.vertex_indices) {
        const core::Vec3 bind = mesh.bind_vertices[vi];
        const core::Vec3 pivot = project_on_crease(bind, *crease);
        const core::Vec3 rel{bind.x - pivot.x, bind.y - pivot.y,
                             bind.z - pivot.z};
        const core::Vec3 rotated = rotate_around(rel, axis, angle);
        pose.vertices[vi] = core::Vec3{pivot.x + rotated.x, pivot.y + rotated.y,
                                       pivot.z + rotated.z};
      }
    }

    double depth_sum = 0.0;
    for (std::uint32_t vi : seg.vertex_indices) {
      depth_sum += pose.vertices[vi].z;
    }
    const double depth =
        depth_sum / static_cast<double>(seg.vertex_indices.size());

    pose.draw_order.push_back(PaperFoldSegmentPose{
        .segment_id = seg.id,
        .crease_id = seg.crease_id,
        .angle_rad = angle,
        .occlusion_order = seg.occlusion_order,
        .depth_z = depth,
    });
  }

  // Back-to-front: smaller (more negative) z first; then occlusion_order.
  std::sort(pose.draw_order.begin(), pose.draw_order.end(),
            [](const PaperFoldSegmentPose &a, const PaperFoldSegmentPose &b) {
              if (!core::nearly_equal(a.depth_z, b.depth_z, 1e-6, 1e-6)) {
                return a.depth_z < b.depth_z;
              }
              if (a.occlusion_order != b.occlusion_order) {
                return a.occlusion_order < b.occlusion_order;
              }
              return a.segment_id < b.segment_id;
            });

  return pose;
}

std::expected<PaperFoldPose, diagnostics::Error>
evaluate_paper_fold_clip(const PaperFoldMesh &mesh, const PaperFoldClip &clip,
                         core::FrameIndex frame) {
  auto angles = sample_paper_fold_angles(clip, mesh, frame);
  if (!angles) {
    return std::unexpected(angles.error());
  }
  return evaluate_paper_fold(mesh, *angles);
}

std::string format_paper_fold_golden(std::string_view label,
                                     const PaperFoldMesh &mesh,
                                     const PaperFoldPose &pose,
                                     core::FrameIndex frame) {
  std::ostringstream os;
  os << "paper_fold " << label << '\n';
  os << "mesh=" << mesh.id << " frame=" << frame.value()
     << " verts=" << pose.vertices.size()
     << " segments=" << pose.draw_order.size() << '\n';
  for (const auto &seg : pose.draw_order) {
    os << "seg " << seg.segment_id
       << " crease=" << (seg.crease_id.empty() ? "-" : seg.crease_id)
       << " ang=" << std::fixed << std::setprecision(6) << seg.angle_rad
       << " z=" << seg.depth_z << " occ=" << seg.occlusion_order << '\n';
  }
  for (std::size_t i = 0; i < pose.vertices.size(); ++i) {
    const auto &v = pose.vertices[i];
    os << "v " << i << " x=" << std::setprecision(6) << v.x << " y=" << v.y
       << " z=" << v.z << '\n';
  }
  return os.str();
}

} // namespace instantee::render_core
