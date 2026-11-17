#include <instantee/geometry/path.hpp>
#include <instantee/geometry/path_cache.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>

#if defined(TESSELATOR_H) || defined(TESS_UNDEF)
#error "geometry public tests must not pull in libtess2 headers"
#endif

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

using instantee::geometry::Contour2f;
using instantee::geometry::FillRule;
using instantee::geometry::FlattenedPath;
using instantee::geometry::FlattenLimits;
using instantee::geometry::Path2f;
using instantee::geometry::PathCommand;
using instantee::geometry::PathGeometryCache;
using instantee::geometry::PathVerb;
using instantee::geometry::StrokeCap;
using instantee::geometry::StrokeJoin;
using instantee::geometry::StrokeStyle;
using instantee::geometry::TessellateLimits;
using instantee::geometry::TriangleMesh2f;
using instantee::geometry::Vec2f;

[[nodiscard]] Path2f rect_path(float x0, float y0, float x1, float y1,
                               bool close = true) {
  Path2f path;
  path.commands.push_back(
      PathCommand{.verb = PathVerb::move_to, .p0 = {x0, y0}});
  path.commands.push_back(
      PathCommand{.verb = PathVerb::line_to, .p0 = {x1, y0}});
  path.commands.push_back(
      PathCommand{.verb = PathVerb::line_to, .p0 = {x1, y1}});
  path.commands.push_back(
      PathCommand{.verb = PathVerb::line_to, .p0 = {x0, y1}});
  if (close) {
    path.commands.push_back(PathCommand{.verb = PathVerb::close});
  }
  return path;
}

[[nodiscard]] float signed_area(const TriangleMesh2f &mesh) {
  float area = 0.0f;
  for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const auto &a = mesh.vertices[mesh.indices[i]];
    const auto &b = mesh.vertices[mesh.indices[i + 1]];
    const auto &c = mesh.vertices[mesh.indices[i + 2]];
    area += (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
  }
  return 0.5f * area;
}

[[nodiscard]] std::size_t triangle_count(const TriangleMesh2f &mesh) {
  return mesh.indices.size() / 3;
}

} // namespace

int main() {
  // Unit square fill (CCW) → two triangles, area ≈ 1.
  {
    auto path = rect_path(0, 0, 1, 1);
    auto flat = instantee::geometry::flatten_path(path);
    expect(flat.has_value() && flat->contours.size() == 1,
           "flatten unit square");
    auto mesh = instantee::geometry::tessellate_fill(*flat, FillRule::nonzero);
    expect(mesh.has_value(), "tessellate unit square");
    expect(triangle_count(*mesh) == 2, "unit square triangle count");
    expect(std::fabs(std::fabs(signed_area(*mesh)) - 1.0f) < 1e-4f,
           "unit square area");
  }

  // Complex path with hole: outer 0..10, hole 3..7 (opposite winding via
  // reverse point order). Nonzero fill area ≈ 100 - 16 = 84.
  {
    Path2f path = rect_path(0, 0, 10, 10);
    // Hole CW relative to outer CCW.
    path.commands.push_back(
        PathCommand{.verb = PathVerb::move_to, .p0 = {3, 3}});
    path.commands.push_back(
        PathCommand{.verb = PathVerb::line_to, .p0 = {3, 7}});
    path.commands.push_back(
        PathCommand{.verb = PathVerb::line_to, .p0 = {7, 7}});
    path.commands.push_back(
        PathCommand{.verb = PathVerb::line_to, .p0 = {7, 3}});
    path.commands.push_back(PathCommand{.verb = PathVerb::close});

    auto flat = instantee::geometry::flatten_path(path);
    expect(flat.has_value() && flat->contours.size() == 2, "flatten hole path");
    auto mesh = instantee::geometry::tessellate_fill(*flat, FillRule::nonzero);
    expect(mesh.has_value() && triangle_count(*mesh) >= 4, "hole tessellates");
    expect(std::fabs(std::fabs(signed_area(*mesh)) - 84.0f) < 0.5f,
           "hole area nonzero");
  }

  // Even-odd vs nonzero on overlapping same-winding squares.
  {
    Path2f path = rect_path(0, 0, 4, 4);
    Path2f inner = rect_path(1, 1, 3, 3);
    path.commands.insert(path.commands.end(), inner.commands.begin(),
                         inner.commands.end());
    auto flat = instantee::geometry::flatten_path(path);
    expect(flat.has_value(), "flatten overlapping");
    auto odd = instantee::geometry::tessellate_fill(*flat, FillRule::even_odd);
    auto nonz = instantee::geometry::tessellate_fill(*flat, FillRule::nonzero);
    expect(odd.has_value() && nonz.has_value(), "winding variants tessellate");
    const float odd_area = std::fabs(signed_area(*odd));
    const float nonz_area = std::fabs(signed_area(*nonz));
    expect(std::fabs(odd_area - 12.0f) < 0.5f, "even-odd punches overlap");
    expect(nonz_area + 0.5f > odd_area, "nonzero keeps overlap filled");
  }

  // Degenerate / empty inputs.
  {
    FlattenedPath empty{};
    auto mesh = instantee::geometry::tessellate_fill(empty);
    expect(mesh.has_value() && mesh->indices.empty(), "empty path mesh");

    FlattenedPath collinear;
    collinear.contours.push_back(Contour2f{.points = {{0, 0}, {1, 0}, {2, 0}}});
    auto deg = instantee::geometry::tessellate_fill(collinear);
    // libtess2 may succeed with zero area or reject; either is acceptable if
    // it does not crash and stays within limits.
    expect(deg.has_value() || !deg.error().code.empty(),
           "collinear handled without crash");
    if (deg) {
      expect(std::fabs(signed_area(*deg)) < 1e-3f, "collinear near-zero area");
    }
  }

  // Flatten budgets.
  {
    Path2f path = rect_path(0, 0, 1, 1);
    FlattenLimits tight{.max_contours = 0};
    auto bad = instantee::geometry::flatten_path(path, tight);
    expect(!bad.has_value() && bad.error().code == "geometry.flatten.limit",
           "contour budget");
  }

  // Stroke open polyline and closed path.
  {
    std::vector<Vec2f> line{{0, 0}, {10, 0}};
    StrokeStyle style{
        .width = 2.0f, .join = StrokeJoin::miter, .cap = StrokeCap::butt};
    auto stroke = instantee::geometry::stroke_polyline(line, false, style);
    expect(stroke.has_value() && triangle_count(*stroke) == 2,
           "open stroke two tris");

    auto closed = rect_path(0, 0, 4, 4);
    auto stroked = instantee::geometry::stroke_path(
        closed, StrokeStyle{.width = 1.0f,
                            .join = StrokeJoin::bevel,
                            .cap = StrokeCap::butt});
    expect(stroked.has_value() && triangle_count(*stroked) >= 8,
           "closed stroke has segments");

    auto zero = instantee::geometry::stroke_polyline(
        line, false, StrokeStyle{.width = 0.0f});
    expect(!zero.has_value(), "zero width rejected");
  }

  // Static geometry compile-once cache: repeated lookup does not recompile.
  {
    PathGeometryCache cache;
    auto path = rect_path(0, 0, 2, 2);
    auto a = cache.get_or_compile_fill(path, FillRule::nonzero);
    auto b = cache.get_or_compile_fill(path, FillRule::nonzero);
    expect(a.has_value() && b.has_value(), "cache fill compile");
    expect(*a == *b, "cache returns same mesh pointer");
    expect(cache.stats().compiles == 1, "single compile");
    expect(cache.stats().hits == 1, "second lookup is hit");
    expect(cache.stats().misses == 1, "first lookup is miss");

    // Simulate many frames of static geometry.
    for (int i = 0; i < 60; ++i) {
      auto again = cache.get_or_compile_fill(path, FillRule::nonzero);
      expect(again.has_value() && *again == *a, "per-frame reuse");
    }
    expect(cache.stats().compiles == 1, "no per-frame re-tessellate");
    expect(cache.stats().hits == 61, "sixty warm frames hit cache");

    auto stroked =
        cache.get_or_compile_stroke(path, StrokeStyle{.width = 1.5f,
                                                      .join = StrokeJoin::round,
                                                      .cap = StrokeCap::round});
    auto stroked2 =
        cache.get_or_compile_stroke(path, StrokeStyle{.width = 1.5f,
                                                      .join = StrokeJoin::round,
                                                      .cap = StrokeCap::round});
    expect(stroked.has_value() && stroked2.has_value() && *stroked == *stroked2,
           "stroke cache reuse");
    expect(cache.stats().compiles == 2, "stroke compiled once");
  }

  // Cubic flatten produces enough samples for a non-empty fill.
  {
    Path2f path;
    path.commands.push_back(
        PathCommand{.verb = PathVerb::move_to, .p0 = {0, 0}});
    path.commands.push_back(PathCommand{
        .verb = PathVerb::cubic_to, .p0 = {10, 0}, .p1 = {2, 8}, .p2 = {8, 8}});
    path.commands.push_back(
        PathCommand{.verb = PathVerb::line_to, .p0 = {0, 0}});
    path.commands.push_back(PathCommand{.verb = PathVerb::close});
    auto flat = instantee::geometry::flatten_path(path);
    expect(flat.has_value() && !flat->contours.empty() &&
               flat->contours.front().points.size() >= 4,
           "cubic flatten samples");
    auto mesh = instantee::geometry::tessellate_fill(*flat);
    expect(mesh.has_value() && triangle_count(*mesh) >= 1, "cubic fill");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "path_tessellation_test ok\n";
  return EXIT_SUCCESS;
}
