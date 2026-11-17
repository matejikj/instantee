#include <instantee/render_core/editorial_charts.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
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

[[nodiscard]] Rgba8 at(const Rgba8Image &img, std::uint32_t x,
                       std::uint32_t y) {
  const std::size_t idx = (static_cast<std::size_t>(y) * img.width_px + x) * 4U;
  return Rgba8{img.rgba[idx], img.rgba[idx + 1], img.rgba[idx + 2],
               img.rgba[idx + 3]};
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

[[nodiscard]] bool write_if_missing(const std::filesystem::path &path,
                                    std::string_view body) {
  if (std::filesystem::exists(path)) {
    return true;
  }
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out << body;
  return static_cast<bool>(out);
}

[[nodiscard]] bool only_shared_kinds(const std::vector<DrawOp> &ops) {
  for (const auto &op : ops) {
    switch (op.kind) {
    case DrawOpKind::fill_rect:
    case DrawOpKind::fill_ellipse:
    case DrawOpKind::fill_rounded_rect:
    case DrawOpKind::stroke_line:
      break;
    case DrawOpKind::clear:
      return false;
    }
  }
  return !ops.empty();
}

[[nodiscard]] std::string visual_dump(std::string_view name,
                                      const ChartScene &scene,
                                      const PassDesc &pass,
                                      const Rgba8Image &img) {
  std::ostringstream ss;
  ss << "name=" << name << " w=" << scene.width_px << " h=" << scene.height_px
     << " charts=" << scene.charts.size() << " ops=" << pass.ops.size() << '\n';
  for (const auto &chart : scene.charts) {
    ss << "kind=" << chart_kind_id(chart_kind(chart)) << '\n';
  }
  // Meter fill, bar body, counter badge, comparison, and clear corner.
  const std::uint32_t samples[][2] = {{40, 4}, {22, 24}, {58, 8},
                                      {8, 8},  {35, 4},  {0, 0}};
  for (const auto &s : samples) {
    const Rgba8 c = at(img, s[0], s[1]);
    ss << "p" << s[0] << "," << s[1] << "=" << static_cast<int>(c.r) << ","
       << static_cast<int>(c.g) << "," << static_cast<int>(c.b) << "\n";
  }
  return ss.str();
}

} // namespace

int main(int argc, char **argv) {
  const bool write_goldens =
      argc == 3 && std::string_view(argv[2]) == "--write-goldens";
  if (argc != 2 && !write_goldens) {
    std::cerr << "usage: editorial_charts_test <repo-root> [--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  const auto golden_dir = root / "contracts/fixtures/editorial-charts/goldens";

  // Units + zero-domain coverage (no division by zero).
  {
    auto z = chart_unit_coverage(10.0, 0.0);
    expect(z.has_value() && *z == 0.0, "zero domain → 0 coverage");
    auto half = chart_unit_coverage(50.0, 100.0);
    expect(half.has_value() && std::fabs(*half - 0.5) < 1e-12, "50/100");
    expect(!chart_unit_coverage(1.0, -1.0).has_value(), "reject neg domain");
  }

  // Interpolation keeps unit identity.
  {
    ChartSeries from{.unit = ChartUnit::percent, .values = {0.0, 20.0, 40.0}};
    ChartSeries to{.unit = ChartUnit::percent, .values = {100.0, 80.0, 60.0}};
    auto mid = interpolate_chart_series(from, to, 0.5);
    expect(mid.has_value(), "interp ok");
    if (mid) {
      expect(mid->unit == ChartUnit::percent, "unit preserved");
      expect(std::fabs(mid->values[0] - 50.0) < 1e-12, "mid0");
      expect(std::fabs(mid->values[1] - 50.0) < 1e-12, "mid1");
      expect(std::fabs(mid->values[2] - 50.0) < 1e-12, "mid2");
    }
    ChartSeries bad_unit = to;
    bad_unit.unit = ChartUnit::count;
    expect(!interpolate_chart_series(from, bad_unit, 0.5).has_value(),
           "unit mismatch");
    ChartSeries bad_len = to;
    bad_len.values.push_back(1.0);
    expect(!interpolate_chart_series(from, bad_len, 0.25).has_value(),
           "length mismatch");
  }

  // Animated recalculation: lerp series then lower bars.
  {
    ChartSeries a{.unit = ChartUnit::count, .values = {0.0, 0.0, 0.0}};
    ChartSeries b{.unit = ChartUnit::count, .values = {10.0, 20.0, 30.0}};
    auto live = interpolate_chart_series(a, b, 0.5);
    expect(live.has_value(), "anim series");
    if (live) {
      BarChartSpec bars{.plot = {.x = 4, .y = 8, .width = 40, .height = 28},
                        .series = *live,
                        .domain_max = 30.0,
                        .gap_px = 2.0f,
                        .bar_color = {0, 0, 0, 255}};
      auto ops = lower_chart(bars);
      expect(ops.has_value() && only_shared_kinds(*ops), "bar shared ops");
    }
  }

  // Counter digit quantization + percent meter.
  {
    auto d = chart_counter_digit(75.0, ChartUnit::percent);
    expect(d.has_value() && *d == 8, "75% → digit 8");
    expect(!chart_counter_digit(-1.0, ChartUnit::count).has_value(),
           "reject neg count");
    auto meter = lower_chart(
        PercentMeterSpec{.track = {.x = 2, .y = 2, .width = 40, .height = 6},
                         .percent = 50.0,
                         .corner_radius = 1.0f});
    expect(meter.has_value() && only_shared_kinds(*meter), "meter ops");
  }

  // Visual catalog golden.
  {
    ChartScene scene;
    scene.width_px = 64;
    scene.height_px = 48;
    scene.clear_color = {255, 255, 255, 255};

    ChartSeries counts{.unit = ChartUnit::count, .values = {5.0, 15.0, 25.0}};
    scene.charts.push_back(
        BarChartSpec{.plot = {.x = 2, .y = 18, .width = 28, .height = 26},
                     .series = counts,
                     .domain_max = 30.0,
                     .gap_px = 2.0f,
                     .bar_color = {0, 0, 0, 255}});
    scene.charts.push_back(
        LineChartSpec{.plot = {.x = 34, .y = 18, .width = 28, .height = 26},
                      .series = counts,
                      .domain_max = 30.0,
                      .stroke_width = 2.0f,
                      .line_color = {0, 0, 0, 255}});
    scene.charts.push_back(ComparisonChartSpec{
        .plot = {.x = 2, .y = 2, .width = 28, .height = 12},
        .left = ChartSeries{.unit = ChartUnit::ratio, .values = {0.4, 0.8}},
        .right = ChartSeries{.unit = ChartUnit::ratio, .values = {0.7, 0.3}},
        .domain_max = 1.0,
        .gap_px = 2.0f,
        .pair_gap_px = 1.0f,
        .left_color = {0, 0, 0, 255},
        .right_color = {198, 255, 0, 255}});
    scene.charts.push_back(
        PercentMeterSpec{.track = {.x = 34, .y = 4, .width = 20, .height = 6},
                         .percent = 60.0,
                         .corner_radius = 2.0f});
    scene.charts.push_back(CounterSpec{.cx = 58,
                                       .cy = 8,
                                       .radius = 5.0f,
                                       .value = 3.0,
                                       .unit = ChartUnit::count});

    auto pass = compose_chart_pass(scene);
    expect(pass.has_value(), "compose");
    if (pass) {
      std::vector<DrawOp> body(pass->ops.begin() + 1, pass->ops.end());
      expect(only_shared_kinds(body), "catalog shared kinds");
    }
    auto img = rasterize_chart_scene(scene);
    expect(img.has_value(), "raster");
    if (pass && img) {
      expect(at(*img, 22, 24).r == 0, "bar ink sample");
      expect(at(*img, 40, 4).r == 0, "meter fill sample");
      const std::string golden = visual_dump("catalog", scene, *pass, *img);
      const auto path = golden_dir / "catalog.txt";
      if (write_goldens) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << golden;
        expect(static_cast<bool>(out), "write golden");
      } else {
        expect(write_if_missing(path, golden), "seed golden");
        expect(read_file(path) == golden, "golden match");
      }
      auto again = rasterize_chart_scene(scene);
      expect(again.has_value() && again->rgba == img->rgba, "deterministic");
    }
  }

  // Domain zero bars produce baseline only (no crash / no div0).
  {
    auto ops = lower_chart(
        BarChartSpec{.plot = {.x = 0, .y = 0, .width = 20, .height = 10},
                     .series = {.unit = ChartUnit::count, .values = {1.0, 2.0}},
                     .domain_max = 0.0});
    expect(ops.has_value() && ops->size() == 1, "zero domain baseline only");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
