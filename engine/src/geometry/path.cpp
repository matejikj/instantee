#include <instantee/geometry/path.hpp>

#include <cmath>
#include <format>
#include <sstream>

namespace instantee::geometry {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "geometry", message};
}

float dist(Vec2f a, Vec2f b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return std::sqrt(dx * dx + dy * dy);
}

void append_point(Contour2f &contour, Vec2f p, float min_spacing) {
  if (!contour.points.empty()) {
    const auto &last = contour.points.back();
    if (dist(last, p) < min_spacing) {
      return;
    }
  }
  contour.points.push_back(p);
}

void flatten_quad(Contour2f &out, Vec2f p0, Vec2f p1, Vec2f p2, float tol,
                  std::uint32_t depth, const FlattenLimits &limits,
                  diagnostics::Error &error) {
  if (!error.code.empty()) {
    return;
  }
  if (out.points.size() >= limits.max_points_per_contour) {
    error =
        make_invalid("geometry.flatten.limit", "contour point budget exceeded");
    return;
  }
  const float dx = p2.x - p0.x;
  const float dy = p2.y - p0.y;
  const float d = std::abs((p1.x - p2.x) * dy - (p1.y - p2.y) * dx);
  const float chord2 = dx * dx + dy * dy;
  if (depth > 16 || d * d <= tol * tol * chord2 || chord2 < 1e-12f) {
    append_point(out, p2, 1e-5f);
    return;
  }
  const Vec2f p01{(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
  const Vec2f p12{(p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f};
  const Vec2f mid{(p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f};
  flatten_quad(out, p0, p01, mid, tol, depth + 1, limits, error);
  flatten_quad(out, mid, p12, p2, tol, depth + 1, limits, error);
}

void flatten_cubic(Contour2f &out, Vec2f p0, Vec2f p1, Vec2f p2, Vec2f p3,
                   float tol, std::uint32_t depth, const FlattenLimits &limits,
                   diagnostics::Error &error) {
  if (!error.code.empty()) {
    return;
  }
  if (out.points.size() >= limits.max_points_per_contour) {
    error =
        make_invalid("geometry.flatten.limit", "contour point budget exceeded");
    return;
  }
  const float dx = p3.x - p0.x;
  const float dy = p3.y - p0.y;
  const float d1 = std::abs((p1.x - p3.x) * dy - (p1.y - p3.y) * dx);
  const float d2 = std::abs((p2.x - p3.x) * dy - (p2.y - p3.y) * dx);
  const float chord2 = dx * dx + dy * dy;
  if (depth > 16 || (d1 + d2) * (d1 + d2) <= tol * tol * chord2 ||
      chord2 < 1e-12f) {
    append_point(out, p3, 1e-5f);
    return;
  }
  const Vec2f p01{(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
  const Vec2f p12{(p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f};
  const Vec2f p23{(p2.x + p3.x) * 0.5f, (p2.y + p3.y) * 0.5f};
  const Vec2f p012{(p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f};
  const Vec2f p123{(p12.x + p23.x) * 0.5f, (p12.y + p23.y) * 0.5f};
  const Vec2f mid{(p012.x + p123.x) * 0.5f, (p012.y + p123.y) * 0.5f};
  flatten_cubic(out, p0, p01, p012, mid, tol, depth + 1, limits, error);
  flatten_cubic(out, mid, p123, p23, p3, tol, depth + 1, limits, error);
}

void append_key_point(std::ostringstream &os, Vec2f p) {
  os << std::format("{:.6g},{:.6g};", p.x, p.y);
}

} // namespace

std::string path_content_key(const Path2f &path, FillRule fill_rule,
                             float flatten_tolerance) {
  std::ostringstream os;
  os << "fill|" << static_cast<int>(fill_rule) << "|"
     << std::format("{:.6g}|", flatten_tolerance);
  for (const auto &cmd : path.commands) {
    os << static_cast<int>(cmd.verb) << ':';
    append_key_point(os, cmd.p0);
    if (cmd.verb == PathVerb::quad_to || cmd.verb == PathVerb::cubic_to) {
      append_key_point(os, cmd.p1);
    }
    if (cmd.verb == PathVerb::cubic_to) {
      append_key_point(os, cmd.p2);
    }
  }
  return os.str();
}

std::string stroke_content_key(const Path2f &path, const StrokeStyle &style,
                               float flatten_tolerance) {
  std::ostringstream os;
  os << "stroke|" << std::format("{:.6g}|", style.width)
     << static_cast<int>(style.join) << '|' << static_cast<int>(style.cap)
     << '|'
     << std::format("{:.6g}|{:.6g}|", style.miter_limit, flatten_tolerance);
  for (const auto &cmd : path.commands) {
    os << static_cast<int>(cmd.verb) << ':';
    append_key_point(os, cmd.p0);
    if (cmd.verb == PathVerb::quad_to || cmd.verb == PathVerb::cubic_to) {
      append_key_point(os, cmd.p1);
    }
    if (cmd.verb == PathVerb::cubic_to) {
      append_key_point(os, cmd.p2);
    }
  }
  return os.str();
}

std::expected<FlattenedPath, diagnostics::Error>
flatten_path(const Path2f &path, const FlattenLimits &limits) {
  FlattenedPath out;
  Contour2f current;
  Vec2f pen{};
  Vec2f subpath_start{};
  bool have_pen = false;
  std::uint32_t total_points = 0;
  diagnostics::Error error{};

  auto flush_contour = [&]() {
    if (current.points.size() >= 3) {
      if (out.contours.size() >= limits.max_contours) {
        error =
            make_invalid("geometry.flatten.limit", "contour budget exceeded");
        return;
      }
      total_points += static_cast<std::uint32_t>(current.points.size());
      if (total_points > limits.max_total_points) {
        error = make_invalid("geometry.flatten.limit",
                             "total point budget exceeded");
        return;
      }
      out.contours.push_back(std::move(current));
    }
    current = Contour2f{};
  };

  for (const auto &cmd : path.commands) {
    if (!error.code.empty()) {
      return std::unexpected(error);
    }
    switch (cmd.verb) {
    case PathVerb::move_to:
      flush_contour();
      if (!error.code.empty()) {
        return std::unexpected(error);
      }
      pen = cmd.p0;
      subpath_start = cmd.p0;
      have_pen = true;
      append_point(current, pen, 0.0f);
      break;
    case PathVerb::line_to:
      if (!have_pen) {
        pen = cmd.p0;
        subpath_start = cmd.p0;
        have_pen = true;
        append_point(current, pen, 0.0f);
      } else {
        append_point(current, cmd.p0, 1e-5f);
        pen = cmd.p0;
      }
      break;
    case PathVerb::quad_to:
      if (!have_pen) {
        return std::unexpected(make_invalid("geometry.flatten.invalid",
                                            "quad_to without current point"));
      }
      flatten_quad(current, pen, cmd.p1, cmd.p0, limits.tolerance, 0, limits,
                   error);
      pen = cmd.p0;
      break;
    case PathVerb::cubic_to:
      if (!have_pen) {
        return std::unexpected(make_invalid("geometry.flatten.invalid",
                                            "cubic_to without current point"));
      }
      flatten_cubic(current, pen, cmd.p1, cmd.p2, cmd.p0, limits.tolerance, 0,
                    limits, error);
      pen = cmd.p0;
      break;
    case PathVerb::close:
      if (have_pen) {
        append_point(current, subpath_start, 1e-5f);
        flush_contour();
        if (!error.code.empty()) {
          return std::unexpected(error);
        }
        have_pen = false;
      }
      break;
    }
  }
  flush_contour();
  if (!error.code.empty()) {
    return std::unexpected(error);
  }
  return out;
}

} // namespace instantee::geometry
