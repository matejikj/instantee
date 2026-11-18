#include <instantee/geometry/paper_cut.hpp>
#include <instantee/geometry/path_cache.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using instantee::geometry::compile_paper_cut;
using instantee::geometry::format_paper_cut_geometry_golden;
using instantee::geometry::get_or_compile_paper_cut;
using instantee::geometry::paper_cut_content_key;
using instantee::geometry::paper_cut_defaults_v1;
using instantee::geometry::PaperCutBounds;
using instantee::geometry::PaperCutParams;
using instantee::geometry::PathGeometryCache;
using instantee::geometry::validate_paper_cut_params;

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

[[nodiscard]] bool same_mesh(const instantee::geometry::TriangleMesh2f &a,
                             const instantee::geometry::TriangleMesh2f &b) {
  if (a.vertices.size() != b.vertices.size() ||
      a.indices.size() != b.indices.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.vertices.size(); ++i) {
    if (a.vertices[i].x != b.vertices[i].x ||
        a.vertices[i].y != b.vertices[i].y) {
      return false;
    }
  }
  return a.indices == b.indices;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: paper_cut_test <source_root> [--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  bool write_goldens = false;
  for (int i = 2; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--write-goldens") {
      write_goldens = true;
    }
  }

  const PaperCutBounds bounds{
      .min_x = 40.0f, .min_y = 60.0f, .max_x = 280.0f, .max_y = 220.0f};
  PaperCutParams params = paper_cut_defaults_v1();
  params.seed = 42;
  params.amplitude_px = 4.0f;
  params.wavelength_px = 10.0f;
  params.safe_inset_px = 14.0f;
  params.samples_per_edge = 12;

  auto geo = compile_paper_cut(bounds, params);
  expect(geo.has_value(), "compile reference cut");
  if (!geo) {
    return EXIT_FAILURE;
  }

  expect(geo->safe_text_bounds.min_x == 54.0f &&
             geo->safe_text_bounds.min_y == 74.0f &&
             geo->safe_text_bounds.max_x == 266.0f &&
             geo->safe_text_bounds.max_y == 206.0f,
         "safe inset bounds");
  expect(!geo->flattened.contours.empty() &&
             geo->flattened.contours.front().points.size() >= 16,
         "outline has samples");
  expect(!geo->fill_mesh.vertices.empty() && !geo->fill_mesh.indices.empty(),
         "fill mesh present");

  // Frame stability: identical params ⇒ bit-identical mesh (no time noise).
  auto again = compile_paper_cut(bounds, params);
  expect(again.has_value() && same_mesh(geo->fill_mesh, again->fill_mesh),
         "stable across frames");

  // Seed change must move the silhouette.
  PaperCutParams other = params;
  other.seed = 99;
  auto alt = compile_paper_cut(bounds, other);
  expect(alt.has_value() && !same_mesh(geo->fill_mesh, alt->fill_mesh),
         "seed affects edges");

  // Amplitude larger than inset is rejected (would invade text).
  {
    PaperCutParams bad = params;
    bad.amplitude_px = 20.0f;
    bad.safe_inset_px = 10.0f;
    expect(!validate_paper_cut_params(bad).has_value(),
           "reject amplitude > inset");
    expect(!compile_paper_cut(bounds, bad).has_value(),
           "compile rejects unsafe amplitude");
  }

  // Bounds too small for inset.
  {
    PaperCutBounds tiny{.min_x = 0, .min_y = 0, .max_x = 10, .max_y = 10};
    expect(!compile_paper_cut(tiny, params).has_value(),
           "reject undersized stock");
  }

  // Zero amplitude yields a near-rectangle; safe region still covered.
  {
    PaperCutParams flat = params;
    flat.amplitude_px = 0.0f;
    auto rectish = compile_paper_cut(bounds, flat);
    expect(rectish.has_value(), "zero-amplitude cut");
  }

  // PathGeometryCache: warm lookups do not recompile.
  {
    PathGeometryCache cache;
    auto a = get_or_compile_paper_cut(cache, bounds, params);
    auto b = get_or_compile_paper_cut(cache, bounds, params);
    expect(a.has_value() && b.has_value(), "cached compiles");
    expect(cache.stats().compiles == 1, "single tessellation compile");
    expect(cache.stats().hits >= 1, "warm frame cache hit");
    expect(same_mesh(a->fill_mesh, b->fill_mesh), "cached meshes match");
  }

  const auto key_a = paper_cut_content_key(bounds, params, 0.25f);
  const auto key_b = paper_cut_content_key(bounds, other, 0.25f);
  expect(key_a != key_b, "content key includes seed");

  const auto golden = format_paper_cut_geometry_golden("rect-seed42", *geo);
  const auto golden_path =
      root / "contracts/fixtures/paper-cut/goldens/rect-seed42.txt";
  expect(check_golden(golden_path, golden, write_goldens),
         "edge geometry golden");

  if (failures != 0) {
    std::cerr << "paper_cut_test failures: " << failures << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "paper_cut_test ok\n";
  return EXIT_SUCCESS;
}
