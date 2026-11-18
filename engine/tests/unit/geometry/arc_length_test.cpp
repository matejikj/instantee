#include <instantee/geometry/arc_length.hpp>
#include <instantee/geometry/path.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using instantee::geometry::ArcLengthTable;
using instantee::geometry::build_arc_length_table;
using instantee::geometry::Path2f;
using instantee::geometry::PathCommand;
using instantee::geometry::PathVerb;
using instantee::geometry::prefix_at_progress;
using instantee::geometry::sample_at_progress;
using instantee::geometry::Vec2f;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] bool near(float a, float b, float eps = 1e-4f) {
  return std::fabs(a - b) <= eps;
}

[[nodiscard]] bool near_pt(Vec2f a, Vec2f b, float eps = 1e-4f) {
  return near(a.x, b.x, eps) && near(a.y, b.y, eps);
}

} // namespace

int main() {
  // Uneven segments: vertices at 0, 1, and 11 on the x-axis (lengths 1 + 10).
  // Mid-index would land on x=1; mid-length must land near x=5.5.
  {
    const std::vector<Vec2f> pts{{0, 0}, {1, 0}, {11, 0}};
    auto table = build_arc_length_table(pts, false);
    expect(table.has_value(), "build uneven polyline");
    if (table) {
      expect(near(table->total_length, 11.0f), "total length 11");
      expect(table->cumulative_s.size() == 3 &&
                 near(table->cumulative_s[1], 1.0f),
             "cumulative at vertex 1");

      auto mid = sample_at_progress(*table, 0.5f);
      expect(mid.has_value() && near_pt(*mid, {5.5f, 0.0f}),
             "progress 0.5 is arc midpoint, not mid vertex");

      auto at_vertex = sample_at_progress(*table, 1.0f / 11.0f);
      expect(at_vertex.has_value() && near_pt(*at_vertex, {1.0f, 0.0f}),
             "progress at first segment end");

      // Index-based fake progress (i / (n-1)) at 0.5 would be vertex 1.
      expect(!near_pt(*mid, {1.0f, 0.0f}),
             "arc mid must differ from index mid");
    }
  }

  // Prefix reveal follows length: at ~1/11 of length the prefix ends at x=1.
  {
    const std::vector<Vec2f> pts{{0, 0}, {1, 0}, {11, 0}};
    auto table = build_arc_length_table(pts);
    expect(table.has_value(), "build for prefix");
    if (table) {
      auto pref = prefix_at_progress(*table, 1.0f / 11.0f);
      expect(pref.has_value() && pref->size() == 2 &&
                 near_pt(pref->back(), {1.0f, 0.0f}),
             "prefix ends at first segment");

      auto half = prefix_at_progress(*table, 0.5f);
      expect(half.has_value() && half->size() == 3 &&
                 near_pt(half->back(), {5.5f, 0.0f}),
             "half prefix ends at arc mid");
    }
  }

  // Closed unit square perimeter = 4; progress 0.25 → (1,0).
  {
    const std::vector<Vec2f> sq{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    auto table = build_arc_length_table(sq, true);
    expect(table.has_value() && near(table->total_length, 4.0f),
           "closed square length");
    if (table) {
      auto p = sample_at_progress(*table, 0.25f);
      expect(p.has_value() && near_pt(*p, {1.0f, 0.0f}),
             "closed progress 0.25");
      auto end = sample_at_progress(*table, 1.0f);
      expect(end.has_value() && near_pt(*end, {0.0f, 0.0f}),
             "closed progress 1 returns start");
    }
  }

  // Path2f open L-shape via flatten.
  {
    Path2f path;
    path.commands.push_back(
        PathCommand{.verb = PathVerb::move_to, .p0 = {0, 0}});
    path.commands.push_back(
        PathCommand{.verb = PathVerb::line_to, .p0 = {2, 0}});
    path.commands.push_back(
        PathCommand{.verb = PathVerb::line_to, .p0 = {2, 6}});
    auto table = build_arc_length_table(path);
    expect(table.has_value() && near(table->total_length, 8.0f),
           "path L length");
    if (table) {
      auto mid = sample_at_progress(*table, 0.5f);
      expect(mid.has_value() && near_pt(*mid, {2.0f, 2.0f}), "L-path arc mid");
    }
  }

  // Errors: non-finite, empty, bad progress.
  {
    const std::vector<Vec2f> bad{{0, 0}, {1, std::nanf("")}};
    expect(!build_arc_length_table(bad).has_value(), "reject nan point");

    const std::vector<Vec2f> one{{0, 0}};
    expect(!build_arc_length_table(one).has_value(), "reject single point");

    const std::vector<Vec2f> ok{{0, 0}, {1, 0}};
    auto table = build_arc_length_table(ok);
    expect(table.has_value(), "build unit segment");
    if (table) {
      auto bad_u = sample_at_progress(*table, std::nanf(""));
      expect(!bad_u.has_value() &&
                 bad_u.error().code == "geometry.arc_length.bad_progress",
             "reject nan progress");
    }
  }

  // Endpoints exact.
  {
    const std::vector<Vec2f> pts{{0, 0}, {3, 4}}; // length 5
    auto table = build_arc_length_table(pts);
    expect(table.has_value() && near(table->total_length, 5.0f), "3-4-5");
    if (table) {
      expect(near_pt(*sample_at_progress(*table, 0.0f), {0, 0}), "u=0");
      expect(near_pt(*sample_at_progress(*table, 1.0f), {3, 4}), "u=1");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "arc_length_test: ok\n";
  return EXIT_SUCCESS;
}
