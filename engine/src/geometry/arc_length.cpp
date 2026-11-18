#include <instantee/geometry/arc_length.hpp>

#include <cmath>
#include <utility>

namespace instantee::geometry {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "geometry", message};
}

[[nodiscard]] bool finite_point(Vec2f p) noexcept {
  return std::isfinite(p.x) && std::isfinite(p.y);
}

[[nodiscard]] float dist(Vec2f a, Vec2f b) noexcept {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return std::sqrt(dx * dx + dy * dy);
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

[[nodiscard]] Vec2f lerp(Vec2f a, Vec2f b, float t) noexcept {
  return Vec2f{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

[[nodiscard]] std::expected<ArcLengthTable, diagnostics::Error>
build_from_cleaned(std::vector<Vec2f> cleaned, bool closed,
                   const ArcLengthLimits &limits) {
  if (cleaned.size() > limits.max_points) {
    return std::unexpected(
        make_invalid("geometry.arc_length.limit", "point budget exceeded"));
  }
  if (cleaned.size() < 2) {
    return std::unexpected(
        make_invalid("geometry.arc_length.invalid",
                     "arc-length table requires at least two distinct points"));
  }

  ArcLengthTable table;
  table.points = std::move(cleaned);
  table.closed = closed;
  table.cumulative_s.resize(table.points.size(), 0.0f);

  float s = 0.0f;
  for (std::size_t i = 1; i < table.points.size(); ++i) {
    const float seg = dist(table.points[i - 1], table.points[i]);
    if (!std::isfinite(seg)) {
      return std::unexpected(make_invalid("geometry.arc_length.invalid",
                                          "non-finite segment length"));
    }
    s += seg;
    table.cumulative_s[i] = s;
  }

  if (closed) {
    const float closing = dist(table.points.back(), table.points.front());
    if (!std::isfinite(closing)) {
      return std::unexpected(make_invalid("geometry.arc_length.invalid",
                                          "non-finite closing length"));
    }
    s += closing;
  }

  table.total_length = s;
  return table;
}

struct SampleLoc {
  std::size_t i0 = 0;
  std::size_t i1 = 0;
  float t = 0.0f; // [0,1] along segment i0→i1
  bool on_closing = false;
};

[[nodiscard]] std::expected<SampleLoc, diagnostics::Error>
locate(const ArcLengthTable &table, float distance) {
  if (table.points.size() < 2 ||
      table.cumulative_s.size() != table.points.size()) {
    return std::unexpected(make_invalid("geometry.arc_length.invalid",
                                        "arc-length table is incomplete"));
  }
  if (!std::isfinite(distance)) {
    return std::unexpected(make_invalid("geometry.arc_length.bad_distance",
                                        "arc distance must be finite"));
  }

  if (!(table.total_length > 0.0f)) {
    return SampleLoc{.i0 = 0, .i1 = 0, .t = 0.0f, .on_closing = false};
  }

  float d = distance;
  if (d < 0.0f) {
    d = 0.0f;
  }
  if (d > table.total_length) {
    d = table.total_length;
  }

  // Exact end: last open point, or start of closed loop.
  if (d >= table.total_length) {
    if (table.closed) {
      return SampleLoc{.i0 = 0, .i1 = 0, .t = 0.0f, .on_closing = false};
    }
    const std::size_t last = table.points.size() - 1;
    return SampleLoc{.i0 = last, .i1 = last, .t = 0.0f, .on_closing = false};
  }

  const float open_len = table.cumulative_s.back();
  if (table.closed && d > open_len) {
    const float seg = table.total_length - open_len;
    const float t = seg > 0.0f ? (d - open_len) / seg : 0.0f;
    return SampleLoc{
        .i0 = table.points.size() - 1, .i1 = 0, .t = t, .on_closing = true};
  }

  // Binary search over cumulative_s.
  std::size_t lo = 0;
  std::size_t hi = table.cumulative_s.size() - 1;
  while (lo + 1 < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (table.cumulative_s[mid] <= d) {
      lo = mid;
    } else {
      hi = mid;
    }
  }

  const float s0 = table.cumulative_s[lo];
  const float s1 = table.cumulative_s[hi];
  const float seg = s1 - s0;
  const float t = seg > 0.0f ? (d - s0) / seg : 0.0f;
  return SampleLoc{.i0 = lo, .i1 = hi, .t = t, .on_closing = false};
}

[[nodiscard]] Vec2f point_at(const ArcLengthTable &table, SampleLoc loc) {
  if (loc.i0 == loc.i1) {
    return table.points[loc.i0];
  }
  return lerp(table.points[loc.i0], table.points[loc.i1], loc.t);
}

} // namespace

std::expected<ArcLengthTable, diagnostics::Error>
build_arc_length_table(std::span<const Vec2f> points, bool closed,
                       const ArcLengthLimits &limits) {
  if (points.size() > limits.max_points) {
    return std::unexpected(
        make_invalid("geometry.arc_length.limit", "point budget exceeded"));
  }

  std::vector<Vec2f> cleaned;
  cleaned.reserve(points.size());
  for (const Vec2f p : points) {
    if (!finite_point(p)) {
      return std::unexpected(
          make_invalid("geometry.arc_length.invalid", "non-finite path point"));
    }
    if (cleaned.empty() || dist(cleaned.back(), p) > 0.0f) {
      cleaned.push_back(p);
    }
  }

  // Closed loops with a duplicated closing vertex stay closed after collapse.
  if (closed && cleaned.size() >= 2 &&
      dist(cleaned.front(), cleaned.back()) == 0.0f) {
    cleaned.pop_back();
  }

  return build_from_cleaned(std::move(cleaned), closed, limits);
}

namespace {

void append_unique_pt(std::vector<Vec2f> &pts, Vec2f p) {
  if (pts.empty() || dist(pts.back(), p) > 1e-5f) {
    pts.push_back(p);
  }
}

void sample_quad_pts(std::vector<Vec2f> &out, Vec2f p0, Vec2f p1, Vec2f p2,
                     float tol) {
  const float dx = p2.x - p0.x;
  const float dy = p2.y - p0.y;
  const float d = std::abs((p1.x - p2.x) * dy - (p1.y - p2.y) * dx);
  const float chord2 = dx * dx + dy * dy;
  if (d * d <= tol * tol * chord2 || chord2 < 1e-12f) {
    append_unique_pt(out, p2);
    return;
  }
  const Vec2f p01{(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
  const Vec2f p12{(p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f};
  const Vec2f mid{(p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f};
  sample_quad_pts(out, p0, p01, mid, tol);
  sample_quad_pts(out, mid, p12, p2, tol);
}

void sample_cubic_pts(std::vector<Vec2f> &out, Vec2f p0, Vec2f p1, Vec2f p2,
                      Vec2f p3, float tol) {
  const float dx = p3.x - p0.x;
  const float dy = p3.y - p0.y;
  const float d1 = std::abs((p1.x - p3.x) * dy - (p1.y - p3.y) * dx);
  const float d2 = std::abs((p2.x - p3.x) * dy - (p2.y - p3.y) * dx);
  const float chord2 = dx * dx + dy * dy;
  if ((d1 + d2) * (d1 + d2) <= tol * tol * chord2 || chord2 < 1e-12f) {
    append_unique_pt(out, p3);
    return;
  }
  const Vec2f p01{(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
  const Vec2f p12{(p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f};
  const Vec2f p23{(p2.x + p3.x) * 0.5f, (p2.y + p3.y) * 0.5f};
  const Vec2f p012{(p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f};
  const Vec2f p123{(p12.x + p23.x) * 0.5f, (p12.y + p23.y) * 0.5f};
  const Vec2f mid{(p012.x + p123.x) * 0.5f, (p012.y + p123.y) * 0.5f};
  sample_cubic_pts(out, p0, p01, p012, mid, tol);
  sample_cubic_pts(out, mid, p123, p23, p3, tol);
}

struct OpenPolyline {
  std::vector<Vec2f> points{};
  bool closed = false;
};

// Stroke-oriented extraction: keep open polylines with ≥2 points. Fill flatten
// drops short open contours, which would break draw-on for simple strokes.
[[nodiscard]] std::vector<OpenPolyline>
extract_stroke_polylines(const Path2f &path, float tolerance) {
  std::vector<OpenPolyline> out;
  OpenPolyline current;
  Vec2f pen{};
  bool have = false;

  auto flush = [&](bool closed) {
    if (current.points.size() >= 2) {
      current.closed = closed;
      out.push_back(std::move(current));
    }
    current = OpenPolyline{};
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
        append_unique_pt(current.points, cmd.p0);
        pen = cmd.p0;
      }
      break;
    case PathVerb::quad_to:
      if (!have) {
        break;
      }
      sample_quad_pts(current.points, pen, cmd.p1, cmd.p0, tolerance);
      pen = cmd.p0;
      break;
    case PathVerb::cubic_to:
      if (!have) {
        break;
      }
      sample_cubic_pts(current.points, pen, cmd.p1, cmd.p2, cmd.p0, tolerance);
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

} // namespace

std::expected<ArcLengthTable, diagnostics::Error>
build_arc_length_table(const Path2f &path, const FlattenLimits &flatten,
                       const ArcLengthLimits &limits) {
  const auto polylines = extract_stroke_polylines(path, flatten.tolerance);
  for (const OpenPolyline &poly : polylines) {
    auto table = build_arc_length_table(std::span<const Vec2f>{poly.points},
                                        poly.closed, limits);
    if (table) {
      return table;
    }
  }
  return std::unexpected(
      make_invalid("geometry.arc_length.invalid",
                   "path has no polyline suitable for arc-length"));
}

std::expected<Vec2f, diagnostics::Error>
sample_at_arc_length(const ArcLengthTable &table, float distance) {
  auto loc = locate(table, distance);
  if (!loc) {
    return std::unexpected(loc.error());
  }
  return point_at(table, *loc);
}

std::expected<Vec2f, diagnostics::Error>
sample_at_progress(const ArcLengthTable &table, float progress) {
  if (!std::isfinite(progress)) {
    return std::unexpected(make_invalid("geometry.arc_length.bad_progress",
                                        "progress must be finite"));
  }
  const float u = clamp01(progress);
  if (!(table.total_length > 0.0f)) {
    if (table.points.empty()) {
      return std::unexpected(make_invalid("geometry.arc_length.invalid",
                                          "empty arc-length table"));
    }
    return table.points.front();
  }
  return sample_at_arc_length(table, u * table.total_length);
}

std::expected<std::vector<Vec2f>, diagnostics::Error>
prefix_at_progress(const ArcLengthTable &table, float progress) {
  if (!std::isfinite(progress)) {
    return std::unexpected(make_invalid("geometry.arc_length.bad_progress",
                                        "progress must be finite"));
  }
  if (table.points.size() < 2 ||
      table.cumulative_s.size() != table.points.size()) {
    return std::unexpected(make_invalid("geometry.arc_length.invalid",
                                        "arc-length table is incomplete"));
  }

  const float u = clamp01(progress);
  if (u <= 0.0f) {
    return std::vector<Vec2f>{table.points.front()};
  }
  if (!(table.total_length > 0.0f) || u >= 1.0f) {
    std::vector<Vec2f> full = table.points;
    if (table.closed) {
      full.push_back(table.points.front());
    }
    return full;
  }

  const float target = u * table.total_length;
  auto loc = locate(table, target);
  if (!loc) {
    return std::unexpected(loc.error());
  }

  std::vector<Vec2f> out;
  out.reserve(loc->i0 + 2);
  for (std::size_t i = 0; i <= loc->i0; ++i) {
    out.push_back(table.points[i]);
  }

  const Vec2f end = point_at(table, *loc);
  if (dist(out.back(), end) > 0.0f) {
    out.push_back(end);
  }
  return out;
}

} // namespace instantee::geometry
