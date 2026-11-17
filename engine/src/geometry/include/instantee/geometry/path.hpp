#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace instantee::geometry {

// Flattened 2D path geometry. Coordinates are in the caller's local space
// (typically canvas pixels). Contours are closed rings; the first contour is
// the outer boundary and subsequent contours are holes.
struct Vec2f {
  float x = 0;
  float y = 0;
};

enum class PathVerb : std::uint8_t {
  move_to = 1,
  line_to = 2,
  quad_to = 3,
  cubic_to = 4,
  close = 5,
};

struct PathCommand {
  PathVerb verb = PathVerb::move_to;
  Vec2f p0{}; // line/quad/cubic end, or move destination
  Vec2f p1{}; // quad control / cubic c1
  Vec2f p2{}; // cubic c2
};

struct Path2f {
  std::vector<PathCommand> commands{};
};

struct Contour2f {
  std::vector<Vec2f> points{};
};

struct FlattenedPath {
  std::vector<Contour2f> contours{};
};

struct TriangleMesh2f {
  std::vector<Vec2f> vertices{};
  std::vector<std::uint32_t> indices{}; // triples
};

enum class FillRule : std::uint8_t {
  nonzero = 1,
  even_odd = 2,
};

enum class StrokeJoin : std::uint8_t {
  miter = 1,
  bevel = 2,
  round = 3,
};

enum class StrokeCap : std::uint8_t {
  butt = 1,
  square = 2,
  round = 3,
};

struct FlattenLimits {
  // Chordal tolerance in path units. Smaller => more segments.
  float tolerance = 0.25f;
  std::uint32_t max_points_per_contour = 4096;
  std::uint32_t max_contours = 64;
  std::uint32_t max_total_points = 16384;
};

struct TessellateLimits {
  std::uint32_t max_triangles = 65536;
};

struct StrokeStyle {
  float width = 1.0f;
  StrokeJoin join = StrokeJoin::miter;
  StrokeCap cap = StrokeCap::butt;
  float miter_limit = 4.0f;
};

// Content identity for compile-once caching. Stable across platforms.
[[nodiscard]] std::string path_content_key(const Path2f &path,
                                           FillRule fill_rule,
                                           float flatten_tolerance);

[[nodiscard]] std::string stroke_content_key(const Path2f &path,
                                             const StrokeStyle &style,
                                             float flatten_tolerance);

[[nodiscard]] std::expected<FlattenedPath, diagnostics::Error>
flatten_path(const Path2f &path, const FlattenLimits &limits = {});

[[nodiscard]] std::expected<TriangleMesh2f, diagnostics::Error>
tessellate_fill(const FlattenedPath &path,
                FillRule fill_rule = FillRule::nonzero,
                const TessellateLimits &limits = {});

// Expand a flattened open/closed polyline into a triangle mesh. Implemented in
// geometry (not libtess2). Degenerate zero-length segments are skipped.
[[nodiscard]] std::expected<TriangleMesh2f, diagnostics::Error>
stroke_polyline(const std::vector<Vec2f> &points, bool closed,
                const StrokeStyle &style, const TessellateLimits &limits = {});

[[nodiscard]] std::expected<TriangleMesh2f, diagnostics::Error>
stroke_path(const Path2f &path, const StrokeStyle &style,
            const FlattenLimits &flatten = {},
            const TessellateLimits &limits = {});

} // namespace instantee::geometry
