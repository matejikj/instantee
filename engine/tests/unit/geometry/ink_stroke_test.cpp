#include <instantee/geometry/ink_stroke.hpp>
#include <instantee/geometry/path.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using instantee::geometry::compile_ink_stroke;
using instantee::geometry::format_ink_stroke_progress_golden;
using instantee::geometry::InkStrokeStyle;
using instantee::geometry::Path2f;
using instantee::geometry::PathCommand;
using instantee::geometry::PathVerb;
using instantee::geometry::sample_ink_stroke_progress;
using instantee::geometry::Vec2f;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path);
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

[[nodiscard]] bool check_golden(const std::filesystem::path &path,
                                std::string_view actual, bool write_goldens) {
  if (write_goldens || !std::filesystem::exists(path)) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out << actual;
    if (write_goldens) {
      return true;
    }
  }
  return read_file(path) == actual;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: ink_stroke_test <source_root> [--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  bool write_goldens = false;
  for (int i = 2; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--write-goldens") {
      write_goldens = true;
    }
  }

  // Uneven polyline: progress follows length, triangle counts rise smoothly.
  const std::vector<Vec2f> pts{{0, 0}, {1, 0}, {11, 0}};
  InkStrokeStyle style;
  style.width_px = 4.0f;
  style.tip_taper = 0.4f;
  style.pressure_amp = 0.25f;
  style.brush_grain = 0.3f;
  style.brush_seed = 7;

  auto mesh = compile_ink_stroke(pts, style, false);
  expect(mesh.has_value(), "compile uneven stroke");
  if (!mesh) {
    return EXIT_FAILURE;
  }
  expect(mesh->total_length > 10.5f && mesh->total_length < 11.5f,
         "length ~11");
  expect(!mesh->vertices.empty() && !mesh->indices.empty(), "has ribbon");

  auto at0 = sample_ink_stroke_progress(*mesh, 0.0f);
  auto at25 = sample_ink_stroke_progress(*mesh, 0.25f);
  auto at50 = sample_ink_stroke_progress(*mesh, 0.5f);
  auto at100 = sample_ink_stroke_progress(*mesh, 1.0f);
  expect(at0 && at25 && at50 && at100, "sample progresses");
  if (at0 && at25 && at50 && at100) {
    expect(at0->visible_triangles == 0, "nothing at 0%");
    expect(at100->visible_triangles == mesh->indices.size() / 3,
           "full at 100%");
    expect(at25->visible_triangles <= at50->visible_triangles &&
               at50->visible_triangles <= at100->visible_triangles,
           "monotonic reveal");
    // Mid-length tip near x=5.5, not mid-vertex x=1.
    expect(std::fabs(at50->tip_x - 5.5f) < 0.15f, "tip follows arc length");
    expect(std::fabs(at50->tip_x - 1.0f) > 1.0f, "tip is not index mid");
  }

  // No bitmap path: progress sampling never allocates rgba buffers — only
  // mesh counters. Variable width: tips narrower than mid.
  {
    const std::size_t pairs = mesh->vertices.size() / 2;
    expect(pairs >= 3, "enough samples");
    const float tip_w = mesh->vertices.front().width_px;
    const float mid_w = mesh->vertices[(pairs / 2) * 2].width_px;
    expect(mid_w > tip_w, "pressure/taper widens mid stroke");
  }

  expect(!sample_ink_stroke_progress(*mesh, std::nanf("")).has_value(),
         "reject nan progress");

  const float progresses[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
  const auto golden =
      format_ink_stroke_progress_golden("uneven-line", *mesh, progresses);
  expect(check_golden(root / "contracts/fixtures/ink-stroke/goldens/"
                             "uneven-line.txt",
                      golden, write_goldens),
         "uneven-line golden");

  // Path2f L-shape progress golden.
  Path2f path;
  path.commands.push_back(PathCommand{.verb = PathVerb::move_to, .p0 = {0, 0}});
  path.commands.push_back(PathCommand{.verb = PathVerb::line_to, .p0 = {2, 0}});
  path.commands.push_back(PathCommand{.verb = PathVerb::line_to, .p0 = {2, 6}});
  auto mesh_l = compile_ink_stroke(path, style);
  expect(mesh_l.has_value(), "compile L path");
  if (mesh_l) {
    const auto g2 =
        format_ink_stroke_progress_golden("path-L", *mesh_l, progresses);
    expect(
        check_golden(root / "contracts/fixtures/ink-stroke/goldens/path-L.txt",
                     g2, write_goldens),
        "path-L golden");
    auto mid = sample_ink_stroke_progress(*mesh_l, 0.5f);
    expect(mid.has_value() && std::fabs(mid->tip_y - 2.0f) < 0.2f,
           "L mid tip on vertical");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "ink_stroke_test: ok\n";
  return EXIT_SUCCESS;
}
