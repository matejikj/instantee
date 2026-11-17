#include <instantee/geometry/path.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace instantee::geometry {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "geometry", message};
}

Vec2f operator+(Vec2f a, Vec2f b) { return {a.x + b.x, a.y + b.y}; }
Vec2f operator-(Vec2f a, Vec2f b) { return {a.x - b.x, a.y - b.y}; }
Vec2f operator*(Vec2f a, float s) { return {a.x * s, a.y * s}; }

float length(Vec2f v) { return std::sqrt(v.x * v.x + v.y * v.y); }

Vec2f normalized(Vec2f v) {
  const float len = length(v);
  if (len < 1e-8f) {
    return {0, 0};
  }
  return v * (1.0f / len);
}

Vec2f perp(Vec2f v) { return {-v.y, v.x}; }

void push_tri(TriangleMesh2f &mesh, Vec2f a, Vec2f b, Vec2f c,
              const TessellateLimits &limits, diagnostics::Error &error) {
  if (!error.code.empty()) {
    return;
  }
  if (mesh.indices.size() / 3 + 1 > limits.max_triangles) {
    error = make_invalid("geometry.stroke.limit", "triangle budget exceeded");
    return;
  }
  const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back(a);
  mesh.vertices.push_back(b);
  mesh.vertices.push_back(c);
  mesh.indices.push_back(base);
  mesh.indices.push_back(base + 1);
  mesh.indices.push_back(base + 2);
}

void push_quad(TriangleMesh2f &mesh, Vec2f a, Vec2f b, Vec2f c, Vec2f d,
               const TessellateLimits &limits, diagnostics::Error &error) {
  push_tri(mesh, a, b, c, limits, error);
  push_tri(mesh, a, c, d, limits, error);
}

void add_round_cap(TriangleMesh2f &mesh, Vec2f center, Vec2f outward,
                   float radius, const TessellateLimits &limits,
                   diagnostics::Error &error) {
  constexpr int kSegments = 8;
  const Vec2f n = normalized(outward);
  if (length(n) < 1e-8f) {
    return;
  }
  const float base_angle = std::atan2(n.y, n.x);
  Vec2f prev{
      center.x +
          std::cos(base_angle - std::numbers::pi_v<float> * 0.5f) * radius,
      center.y +
          std::sin(base_angle - std::numbers::pi_v<float> * 0.5f) * radius};
  for (int i = 1; i <= kSegments; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(kSegments);
    const float ang = base_angle - std::numbers::pi_v<float> * 0.5f +
                      t * std::numbers::pi_v<float>;
    Vec2f cur{center.x + std::cos(ang) * radius,
              center.y + std::sin(ang) * radius};
    push_tri(mesh, center, prev, cur, limits, error);
    prev = cur;
  }
}

void add_round_join(TriangleMesh2f &mesh, Vec2f center, Vec2f from_n,
                    Vec2f to_n, float radius, const TessellateLimits &limits,
                    diagnostics::Error &error) {
  const float a0 = std::atan2(from_n.y, from_n.x);
  float delta = std::atan2(to_n.y, to_n.x) - a0;
  while (delta <= -std::numbers::pi_v<float>) {
    delta += 2.0f * std::numbers::pi_v<float>;
  }
  while (delta > std::numbers::pi_v<float>) {
    delta -= 2.0f * std::numbers::pi_v<float>;
  }
  const int segments =
      std::max(1, static_cast<int>(std::ceil(
                      std::abs(delta) / (std::numbers::pi_v<float> / 8.0f))));
  Vec2f prev{center.x + std::cos(a0) * radius,
             center.y + std::sin(a0) * radius};
  for (int i = 1; i <= segments; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(segments);
    const float ang = a0 + delta * t;
    Vec2f cur{center.x + std::cos(ang) * radius,
              center.y + std::sin(ang) * radius};
    push_tri(mesh, center, prev, cur, limits, error);
    prev = cur;
  }
}

void append_unique(std::vector<Vec2f> &pts, Vec2f p) {
  if (pts.empty() || length(pts.back() - p) >= 1e-5f) {
    pts.push_back(p);
  }
}

// Sample a quadratic into `out` (excluding the start point already in `out`).
void sample_quad(std::vector<Vec2f> &out, Vec2f p0, Vec2f p1, Vec2f p2,
                 float tol) {
  const float dx = p2.x - p0.x;
  const float dy = p2.y - p0.y;
  const float d = std::abs((p1.x - p2.x) * dy - (p1.y - p2.y) * dx);
  const float chord2 = dx * dx + dy * dy;
  if (d * d <= tol * tol * chord2 || chord2 < 1e-12f) {
    append_unique(out, p2);
    return;
  }
  const Vec2f p01{(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
  const Vec2f p12{(p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f};
  const Vec2f mid{(p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f};
  sample_quad(out, p0, p01, mid, tol);
  sample_quad(out, mid, p12, p2, tol);
}

void sample_cubic(std::vector<Vec2f> &out, Vec2f p0, Vec2f p1, Vec2f p2,
                  Vec2f p3, float tol) {
  const float dx = p3.x - p0.x;
  const float dy = p3.y - p0.y;
  const float d1 = std::abs((p1.x - p3.x) * dy - (p1.y - p3.y) * dx);
  const float d2 = std::abs((p2.x - p3.x) * dy - (p2.y - p3.y) * dx);
  const float chord2 = dx * dx + dy * dy;
  if ((d1 + d2) * (d1 + d2) <= tol * tol * chord2 || chord2 < 1e-12f) {
    append_unique(out, p3);
    return;
  }
  const Vec2f p01{(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
  const Vec2f p12{(p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f};
  const Vec2f p23{(p2.x + p3.x) * 0.5f, (p2.y + p3.y) * 0.5f};
  const Vec2f p012{(p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f};
  const Vec2f p123{(p12.x + p23.x) * 0.5f, (p12.y + p23.y) * 0.5f};
  const Vec2f mid{(p012.x + p123.x) * 0.5f, (p012.y + p123.y) * 0.5f};
  sample_cubic(out, p0, p01, p012, mid, tol);
  sample_cubic(out, mid, p123, p23, p3, tol);
}

struct Polyline {
  std::vector<Vec2f> points;
  bool closed = false;
};

[[nodiscard]] std::vector<Polyline> extract_polylines(const Path2f &path,
                                                      float tolerance) {
  std::vector<Polyline> out;
  Polyline current;
  Vec2f pen{};
  bool have = false;

  auto flush = [&](bool closed) {
    if (current.points.size() >= 2) {
      current.closed = closed;
      out.push_back(std::move(current));
    }
    current = Polyline{};
    have = false;
  };

  for (const auto &cmd : path.commands) {
    switch (cmd.verb) {
    case PathVerb::move_to:
      flush(false);
      pen = cmd.p0;
      have = true;
      current.points.push_back(pen);
      break;
    case PathVerb::line_to:
      if (!have) {
        pen = cmd.p0;
        have = true;
        current.points.push_back(pen);
      } else {
        append_unique(current.points, cmd.p0);
        pen = cmd.p0;
      }
      break;
    case PathVerb::quad_to:
      if (!have) {
        break;
      }
      sample_quad(current.points, pen, cmd.p1, cmd.p0, tolerance);
      pen = cmd.p0;
      break;
    case PathVerb::cubic_to:
      if (!have) {
        break;
      }
      sample_cubic(current.points, pen, cmd.p1, cmd.p2, cmd.p0, tolerance);
      pen = cmd.p0;
      break;
    case PathVerb::close:
      flush(true);
      break;
    }
  }
  flush(false);
  return out;
}

void append_mesh(TriangleMesh2f &dst, TriangleMesh2f &&src,
                 const TessellateLimits &limits, diagnostics::Error &error) {
  if (!error.code.empty()) {
    return;
  }
  if (dst.indices.size() / 3 + src.indices.size() / 3 > limits.max_triangles) {
    error = make_invalid("geometry.stroke.limit", "triangle budget exceeded");
    return;
  }
  const auto base = static_cast<std::uint32_t>(dst.vertices.size());
  for (auto &v : src.vertices) {
    dst.vertices.push_back(v);
  }
  for (auto idx : src.indices) {
    dst.indices.push_back(base + idx);
  }
}

} // namespace

std::expected<TriangleMesh2f, diagnostics::Error>
stroke_polyline(const std::vector<Vec2f> &points, bool closed,
                const StrokeStyle &style, const TessellateLimits &limits) {
  if (style.width <= 0.0f) {
    return std::unexpected(make_invalid("geometry.stroke.invalid",
                                        "stroke width must be positive"));
  }
  if (points.size() < 2) {
    return TriangleMesh2f{};
  }

  std::vector<Vec2f> pts;
  pts.reserve(points.size());
  pts.push_back(points.front());
  for (std::size_t i = 1; i < points.size(); ++i) {
    if (length(points[i] - pts.back()) >= 1e-5f) {
      pts.push_back(points[i]);
    }
  }
  if (closed && pts.size() >= 2 && length(pts.front() - pts.back()) < 1e-5f) {
    pts.pop_back();
  }
  if (pts.size() < 2) {
    return TriangleMesh2f{};
  }

  const float half = style.width * 0.5f;
  const std::size_t n = pts.size();
  const std::size_t seg_count = closed ? n : n - 1;

  std::vector<Vec2f> tangents(seg_count);
  std::vector<Vec2f> normals(seg_count);
  for (std::size_t i = 0; i < seg_count; ++i) {
    const Vec2f a = pts[i];
    const Vec2f b = pts[(i + 1) % n];
    tangents[i] = normalized(b - a);
    normals[i] = perp(tangents[i]);
  }

  TriangleMesh2f mesh;
  diagnostics::Error error{};

  auto left = [&](std::size_t i, float side) -> Vec2f {
    return pts[i] + normals[i] * (half * side);
  };
  auto right_end = [&](std::size_t i, float side) -> Vec2f {
    return pts[(i + 1) % n] + normals[i] * (half * side);
  };

  for (std::size_t i = 0; i < seg_count; ++i) {
    push_quad(mesh, left(i, 1.0f), right_end(i, 1.0f), right_end(i, -1.0f),
              left(i, -1.0f), limits, error);
    if (!error.code.empty()) {
      return std::unexpected(error);
    }
  }

  const std::size_t join_start = closed ? 0 : 1;
  const std::size_t join_end = closed ? n : n - 1;
  for (std::size_t i = join_start; i < join_end; ++i) {
    const std::size_t prev_seg = (i + n - 1) % n;
    const std::size_t next_seg = i % n;
    if (prev_seg >= seg_count || next_seg >= seg_count) {
      continue;
    }
    const Vec2f n0 = normals[prev_seg];
    const Vec2f n1 = normals[next_seg];
    const float cross = n0.x * n1.y - n0.y * n1.x;
    if (std::abs(cross) < 1e-6f && n0.x * n1.x + n0.y * n1.y > 0.0f) {
      continue;
    }

    const float side = cross >= 0.0f ? 1.0f : -1.0f;
    if (style.join == StrokeJoin::bevel) {
      push_tri(mesh, pts[i], pts[i] + n0 * (half * side),
               pts[i] + n1 * (half * side), limits, error);
    } else if (style.join == StrokeJoin::round) {
      add_round_join(mesh, pts[i], n0 * side, n1 * side, half, limits, error);
    } else {
      Vec2f miter = normalized(n0 + n1);
      if (length(miter) < 1e-6f) {
        push_tri(mesh, pts[i], pts[i] + n0 * (half * side),
                 pts[i] + n1 * (half * side), limits, error);
      } else {
        const float cos_half =
            std::max(1e-4f, std::abs(miter.x * n0.x + miter.y * n0.y));
        const float miter_len = half / cos_half;
        if (miter_len > half * style.miter_limit) {
          push_tri(mesh, pts[i], pts[i] + n0 * (half * side),
                   pts[i] + n1 * (half * side), limits, error);
        } else {
          const Vec2f tip = pts[i] + miter * (miter_len * side);
          push_tri(mesh, pts[i] + n0 * (half * side), tip,
                   pts[i] + n1 * (half * side), limits, error);
        }
      }
    }
    if (!error.code.empty()) {
      return std::unexpected(error);
    }
  }

  if (!closed) {
    if (style.cap == StrokeCap::square) {
      const Vec2f t0 = tangents.front();
      const Vec2f t1 = tangents.back();
      push_quad(mesh, pts.front() + normals.front() * half - t0 * half,
                pts.front() - normals.front() * half - t0 * half,
                pts.front() - normals.front() * half,
                pts.front() + normals.front() * half, limits, error);
      push_quad(mesh, pts.back() + normals.back() * half,
                pts.back() - normals.back() * half,
                pts.back() - normals.back() * half + t1 * half,
                pts.back() + normals.back() * half + t1 * half, limits, error);
    } else if (style.cap == StrokeCap::round) {
      add_round_cap(mesh, pts.front(), tangents.front() * -1.0f, half, limits,
                    error);
      add_round_cap(mesh, pts.back(), tangents.back(), half, limits, error);
    }
  }

  if (!error.code.empty()) {
    return std::unexpected(error);
  }
  return mesh;
}

std::expected<TriangleMesh2f, diagnostics::Error>
stroke_path(const Path2f &path, const StrokeStyle &style,
            const FlattenLimits &flatten, const TessellateLimits &limits) {
  if (style.width <= 0.0f) {
    return std::unexpected(make_invalid("geometry.stroke.invalid",
                                        "stroke width must be positive"));
  }

  TriangleMesh2f combined;
  diagnostics::Error error{};
  for (const auto &poly : extract_polylines(path, flatten.tolerance)) {
    auto mesh = stroke_polyline(poly.points, poly.closed, style, limits);
    if (!mesh) {
      return std::unexpected(mesh.error());
    }
    append_mesh(combined, std::move(*mesh), limits, error);
    if (!error.code.empty()) {
      return std::unexpected(error);
    }
  }
  return combined;
}

} // namespace instantee::geometry
