#include <instantee/render_core/plot_axes.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__)
#error "render-core tests must remain free of Metal/Apple headers"
#endif

namespace {

using namespace instantee::render_core;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] bool near(float a, float b, float eps = 1e-3f) {
  return std::fabs(a - b) <= eps;
}

} // namespace

int main() {
  AxesSpec axes;
  axes.plot = Rect2f{.x = 40, .y = 20, .width = 200, .height = 100};
  axes.x = AxisDomain{.min = 0, .max = 10, .unit = "s"};
  axes.y = AxisDomain{.min = 0, .max = 5, .unit = "m"};
  axes.target_ticks = 5;
  axes.label_min_gap_px = 2.0f;

  // Domain remap does not mutate caller data arrays.
  std::vector<double> xs{0.0, 5.0, 10.0};
  std::vector<double> ys{0.0, 2.5, 5.0};
  const auto xs_copy = xs;
  const auto ys_copy = ys;

  auto p0 = map_data_to_plot(axes, xs[0], ys[0]);
  auto p1 = map_data_to_plot(axes, xs[1], ys[1]);
  auto p2 = map_data_to_plot(axes, xs[2], ys[2]);
  expect(p0 && p1 && p2, "map corners/mid");
  if (p0 && p1 && p2) {
    expect(near(p0->x, 40.0f) && near(p0->y, 120.0f), "origin bottom-left");
    expect(near(p2->x, 240.0f) && near(p2->y, 20.0f), "max top-right");
    expect(near(p1->x, 140.0f) && near(p1->y, 70.0f), "midpoint");
  }
  expect(xs == xs_copy && ys == ys_copy, "data arrays unchanged after map");

  // Changing domain remaps presentation; data values stay identical.
  axes.x.max = 20;
  auto p1b = map_data_to_plot(axes, xs[1], ys[1]);
  expect(p1b.has_value(), "remap after domain change");
  if (p1 && p1b) {
    expect(!near(p1->x, p1b->x), "x pixel shifts with domain");
    expect(near(p1->y, p1b->y), "y pixel unchanged when only x domain changes");
  }
  expect(xs == xs_copy && ys == ys_copy, "data still unchanged after domain");

  // Round-trip plot → data for mid point under original domain.
  axes.x.max = 10;
  auto mid = map_data_to_plot(axes, 5.0, 2.5);
  expect(mid.has_value(), "mid map");
  if (mid) {
    auto back = map_plot_to_data(axes, mid->x, mid->y);
    expect(back.has_value() && near(back->x, 5.0f) && near(back->y, 2.5f),
           "round trip");
  }

  auto layout = layout_axes(axes);
  expect(layout.has_value(), "layout axes");
  if (layout) {
    expect(!layout->x_ticks.empty() && !layout->y_ticks.empty(), "has ticks");
    expect(!layout->ops.empty(), "has draw ops");
    expect(layout->warnings.empty(), "no collision with roomy plot");
    bool saw_unit = false;
    for (const auto &t : layout->x_ticks) {
      if (t.label.find("s") != std::string::npos) {
        saw_unit = true;
      }
    }
    expect(saw_unit, "x labels include unit");
  }

  // Force label collisions: tiny plot + large min gap + many ticks.
  AxesSpec dense = axes;
  dense.plot.width = 40;
  dense.plot.height = 40;
  dense.target_ticks = 8;
  dense.max_ticks = 16;
  dense.label_min_gap_px = 30.0f;
  auto crowded = layout_axes(dense);
  expect(crowded.has_value(), "crowded layout succeeds");
  if (crowded) {
    expect(!crowded->warnings.empty(), "collision emits warning");
    bool code_ok = false;
    for (const auto &w : crowded->warnings) {
      if (w.code == "axes.label_collision") {
        code_ok = true;
      }
    }
    expect(code_ok, "warning code");
  }

  // Reject zero-width domain.
  AxesSpec bad = axes;
  bad.x.max = bad.x.min;
  expect(!map_data_to_plot(bad, 0, 0).has_value(), "reject flat domain");
  expect(!layout_axes(bad).has_value(), "reject flat domain layout");

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "plot_axes_test: ok\n";
  return EXIT_SUCCESS;
}
