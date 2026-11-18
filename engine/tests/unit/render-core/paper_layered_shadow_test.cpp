#include <instantee/render_core/paper_layered_shadow.hpp>

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

[[nodiscard]] int luma(const Rgba8 &c) {
  return static_cast<int>(0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b);
}

[[nodiscard]] PaperLayeredShadowScene make_three_layer_scene() {
  PaperLayeredShadowScene scene;
  scene.width_px = 32;
  scene.height_px = 32;
  scene.clear_color = {255, 255, 255, 255};
  scene.params.offset_x_px = 4.0f;
  scene.params.offset_y_px = 4.0f;
  scene.params.offset_per_depth = 0.5f;
  scene.params.contact_depth = 2.0f;
  scene.params.contact_spread_px = 1.0f;
  scene.params.soft_radius_px = 2.0f;
  scene.params.max_soft_spread_px = 3.0f;
  scene.params.quality = ShadowQualityTier::medium;
  scene.params.shadow_color = {0, 0, 0, 96};
  scene.params.contact_color = {0, 0, 0, 140};

  // Near card (small depth) overlaps mid; far is separated more.
  scene.layers.push_back(PaperLayerCard{
      .rect = {6, 6, 12, 12},
      .depth = 4.0f,
      .layer_order = 0,
      .fill = {220, 80, 70, 255},
  });
  scene.layers.push_back(PaperLayerCard{
      .rect = {10, 10, 14, 14},
      .depth = 5.5f, // contact range vs near
      .layer_order = 1,
      .fill = {70, 140, 210, 255},
  });
  scene.layers.push_back(PaperLayerCard{
      .rect = {4, 16, 18, 12},
      .depth = 12.0f, // far drop separation
      .layer_order = 2,
      .fill = {240, 220, 160, 255},
  });
  return scene;
}

} // namespace

int main(int argc, char **argv) {
  const std::filesystem::path root = (argc > 1)
                                         ? std::filesystem::path{argv[1]}
                                         : std::filesystem::current_path();
  const auto golden_path =
      root / "contracts/fixtures/paper-layered-shadow/goldens/three-layer.txt";

  {
    PaperLayeredShadowScene bad = make_three_layer_scene();
    bad.layers.pop_back();
    bad.layers.pop_back();
    expect(!validate_paper_layered_shadow_scene(bad).has_value(),
           "reject single layer");
    bad = make_three_layer_scene();
    bad.layers[1].depth = bad.layers[0].depth;
    expect(!validate_paper_layered_shadow_scene(bad).has_value(),
           "reject duplicate depth");
    bad = make_three_layer_scene();
    bad.params.contact_depth = 0.0f;
    expect(!validate_paper_layered_shadow_scene(bad).has_value(),
           "reject zero contact depth");
  }

  auto scene = make_three_layer_scene();
  expect(validate_paper_layered_shadow_scene(scene).has_value(), "scene ok");

  auto casts = resolve_paper_layer_shadow_casts(scene);
  expect(casts.has_value() && casts->size() == 3, "three casts");
  if (casts) {
    expect((*casts)[0].contact, "near→mid uses contact cue");
    expect(!(*casts)[1].contact, "mid→far uses offset drop");
    expect((*casts)[1].offset_x_px > (*casts)[0].offset_x_px,
           "farther separation grows offset");
    expect((*casts)[0].soft_radius_px <= scene.params.max_soft_spread_px &&
               (*casts)[1].soft_radius_px <= scene.params.max_soft_spread_px,
           "soft radius capped against halo spread");
  }

  auto image = rasterize_paper_layered_shadows(scene);
  expect(image.has_value(), "rasterize");
  if (!image) {
    return EXIT_FAILURE;
  }

  {
    // Stage-floor drop lands on background near an offset card silhouette.
    const auto clean = at(*image, 1, 1);
    expect(clean.r > 250 && clean.g > 250 && clean.b > 250, "clean corner");
    // Mid card offset (9,9) from rect starting ~10,10 projects near 19+.
    // Sample a background pixel that should receive floor shadow, not content.
    bool found_floor_shadow = false;
    for (std::uint32_t y = 24; y < 32 && !found_floor_shadow; ++y) {
      for (std::uint32_t x = 24; x < 32; ++x) {
        const auto c = at(*image, x, y);
        if (c.r < 250 || c.g < 250 || c.b < 250) {
          found_floor_shadow = true;
          expect(!(c.r == 0 && c.g == 0 && c.b == 0),
                 "floor shadow not pure black");
          break;
        }
      }
    }
    expect(found_floor_shadow, "visible floor shadow cue");
  }

  {
    // Contact / drop on a farther sheet: mid sheet pixel darkened by near cast
    // but still recognizably blue (content under soft darkening).
    // Mid rect covers [10,24)²; near offset contact ~1.4px. Sample mid-only
    // region away from near footprint: near is [6,18)² so mid-only e.g.
    // (20,12).
    const auto mid = at(*image, 20, 12);
    expect(mid.b > mid.r, "mid sheet still blue-dominant");
    // Compare to an undarkened mid reference is hard after contact; ensure we
    // are not a hard-black halo.
    expect(luma(mid) > 20, "no crushed black halo on mid");
  }

  {
    // Soft halo must not float outside every receiver/floor footprint.
    const auto outside = at(*image, 30, 2);
    expect(outside.r > 250 && outside.g > 250 && outside.b > 250,
           "no soft halo in empty corner");
  }

  {
    // Content order preserved: near-card center stays near-card red, not blue.
    const auto near_center = at(*image, 12, 12);
    expect(near_center.r > near_center.b && near_center.r > 150,
           "near content on top");
  }

  {
    // Inactive / empty soft: hard contact still works with soft_radius 0 path
    // via contact_spread; zero soft_radius on drop still stamps hard offset.
    PaperLayeredShadowScene hard = scene;
    hard.params.soft_radius_px = 0.0f;
    hard.params.contact_spread_px = 0.0f;
    hard.params.max_soft_spread_px = 0.0f;
    auto hard_img = rasterize_paper_layered_shadows(hard);
    expect(hard_img.has_value(), "hard layered path");
  }

  // Probe feature remains registered (capability hook, not a style fork).
  auto registry = make_shadow_shader_feature_registry();
  expect(registry.contains(std::string{kPaperLayeredDepthProbeFeature}),
         "paper layered depth probe still registered");

  const std::string dump =
      format_paper_layered_shadow_golden("three-layer", scene, *image);
  expect(!dump.empty(), "golden dump");
  expect(write_if_missing(golden_path, dump), "write golden if missing");
  const std::string expected = read_file(golden_path);
  expect(!expected.empty(), "golden readable");
  expect(dump == expected, "layered shadow golden match");
  if (dump != expected) {
    std::cerr << "--- expected ---\n" << expected << "--- actual ---\n" << dump;
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
