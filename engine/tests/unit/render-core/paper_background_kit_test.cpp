#include <instantee/render_core/paper_background_kit.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

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

[[nodiscard]] bool scenery_equal(const BackgroundScenery &a,
                                 const BackgroundScenery &b) {
  return format_paper_background_scenery("cmp", a) ==
         format_paper_background_scenery("cmp", b);
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: paper_background_kit_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const std::filesystem::path fixture_dir =
      root / "contracts/fixtures/paper-background";

  BackgroundKitParams params;
  params.seed = 0xB6A7U;
  params.limits = background_kit_limits_v1();
  expect(validate_background_kit_params(params).has_value(), "params ok");

  auto a = generate_paper_background_scenery(params);
  expect(a.has_value(), "generate a");
  if (!a) {
    return EXIT_FAILURE;
  }
  expect(a->asset_count > 0U, "non-empty scenery");
  expect(a->asset_count <= params.limits.max_total_assets, "bounded total");
  expect(a->stage.planes.size() >= 10U && a->stage.planes.size() <= 20U,
         "depth stage layers");

  // Kind coverage + per-kind caps.
  std::unordered_map<BackgroundElementKind, std::uint32_t> counts;
  for (const auto &el : a->elements) {
    ++counts[el.kind];
    expect(el.stage_layer < a->stage.planes.size(), "layer in range");
    if (el.kind == BackgroundElementKind::cloud ||
        el.kind == BackgroundElementKind::building) {
      expect(el.has_prop, "prop present");
      expect(!el.prop.blueprint_id.empty(), "blueprint id");
    } else {
      expect(!el.has_prop, "silhouette kinds");
      expect(!el.silhouette.empty(), "silhouette parts");
    }
  }
  expect(counts[BackgroundElementKind::mountain] >= 1U, "has mountains");
  expect(counts[BackgroundElementKind::cloud] >= 1U, "has clouds");
  expect(counts[BackgroundElementKind::building] >= 1U, "has buildings");
  expect(counts[BackgroundElementKind::tree] >= 1U, "has trees");
  expect(counts[BackgroundElementKind::atmosphere] >= 1U, "has atmosphere");
  expect(counts[BackgroundElementKind::mountain] <= params.limits.max_mountains,
         "mountain cap");
  expect(counts[BackgroundElementKind::cloud] <= params.limits.max_clouds,
         "cloud cap");
  expect(counts[BackgroundElementKind::building] <= params.limits.max_buildings,
         "building cap");
  expect(counts[BackgroundElementKind::tree] <= params.limits.max_trees,
         "tree cap");
  expect(counts[BackgroundElementKind::atmosphere] <=
             params.limits.max_atmosphere,
         "atmosphere cap");

  // Determinism: same seed matches; different seed diverges.
  auto b = generate_paper_background_scenery(params);
  expect(b.has_value() && scenery_equal(*a, *b), "seed stable");

  BackgroundKitParams other = params;
  other.seed = params.seed ^ 0x11U;
  auto c = generate_paper_background_scenery(other);
  expect(c.has_value(), "other seed");
  if (c) {
    expect(!scenery_equal(*a, *c), "seed changes scenery");
  }

  // Hard total cap is respected even if per-kind maxima sum higher.
  {
    BackgroundKitParams tight = params;
    tight.limits.max_total_assets = 10;
    tight.limits.max_mountains = 8;
    tight.limits.max_clouds = 8;
    tight.limits.max_buildings = 8;
    tight.limits.max_trees = 8;
    tight.limits.max_atmosphere = 6;
    auto clipped = generate_paper_background_scenery(tight);
    expect(clipped.has_value(), "tight generate");
    if (clipped) {
      expect(clipped->asset_count <= 10U, "hard total cap");
    }
  }

  // Invalid limits fail closed.
  {
    BackgroundKitParams bad = params;
    bad.limits.stage_layers = 3;
    expect(!validate_background_kit_params(bad).has_value(),
           "bad layer count rejected");
  }

  // Fixture lock.
  const std::string body = format_paper_background_scenery("seed-b6a7", *a);
  const auto path = fixture_dir / "seed-b6a7.txt";
  expect(write_if_missing(path, body), "write fixture");
  expect(read_file(path) == body, "fixture matches");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
