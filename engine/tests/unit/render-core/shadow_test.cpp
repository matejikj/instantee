#include <instantee/render_core/shadow.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
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

[[nodiscard]] bool rgb_eq(const Rgba8 &c, std::uint8_t r, std::uint8_t g,
                          std::uint8_t b, std::uint8_t tol = 0) {
  const auto near = [tol](std::uint8_t a, std::uint8_t e) {
    const int d = static_cast<int>(a) - static_cast<int>(e);
    return d <= static_cast<int>(tol) && d >= -static_cast<int>(tol);
  };
  return near(c.r, r) && near(c.g, g) && near(c.b, b);
}

} // namespace

int main() {
  auto registry = make_shadow_shader_feature_registry();
  expect(registry.contains(std::string{kDropShadowFeature}), "drop_shadow");
  expect(registry.contains(std::string{kSoftShadowFeature}), "soft_shadow");
  expect(registry.contains(std::string{kPaperLayeredDepthProbeFeature}),
         "paper probe registered");
  expect(!registry.contains("paper-cinema"), "no style package key");

  MaterialDesc material{
      .id = "shadowed",
      .version = 1,
      .feature_names = {std::string{kSoftShadowFeature}, "test_pattern",
                        std::string{kPaperLayeredDepthProbeFeature}},
  };

  // Soft shadow + content + paper probe → cast, blur, probe hook, main_color.
  auto active = registry.plan_passes(
      material,
      std::vector<std::string>{std::string{kSoftShadowFeature}, "test_pattern",
                               std::string{kPaperLayeredDepthProbeFeature}});
  expect(active.has_value(), "plan active");
  if (active) {
    // sort_key: cast=10, probe=15, blur=20, main=100
    expect(active->pass_keys.size() == 4, "four logical passes");
    expect(active->pass_keys[0] == "shadow_cast", "cast first");
    expect(active->pass_keys[1] == "paper_depth_shadow_hook", "probe second");
    expect(active->pass_keys[2] == "shadow_blur", "blur third");
    expect(active->pass_keys[3] == "main_color", "content last");
  }

  auto counted = plan_shadow_passes(
      registry, material,
      std::vector<std::string>{std::string{kSoftShadowFeature}, "test_pattern",
                               std::string{kPaperLayeredDepthProbeFeature}});
  expect(counted.has_value(), "counted plan");
  if (counted) {
    expect(counted->shadow_pass_count == 3, "cast+blur+probe");
    expect(counted->content_pass_count == 1, "one content pass");
  }

  // Omitted soft shadow adds no shadow/blur passes.
  auto omitted = plan_shadow_passes(registry, material,
                                    std::vector<std::string>{"test_pattern"});
  expect(omitted.has_value(), "omitted plan");
  if (omitted) {
    expect(omitted->shadow_pass_count == 0, "inactive soft => 0 shadow passes");
    expect(omitted->content_pass_count == 1, "content remains");
    expect(omitted->pass_keys.size() == 1 &&
               omitted->pass_keys[0] == "main_color",
           "only main_color");
  }

  // Hard drop shadow alone.
  MaterialDesc hard_mat{
      .id = "hard",
      .version = 1,
      .feature_names = {std::string{kDropShadowFeature}, "test_pattern"}};
  auto hard_plan =
      plan_shadow_passes(registry, hard_mat,
                         std::vector<std::string>{
                             std::string{kDropShadowFeature}, "test_pattern"});
  expect(hard_plan.has_value() && hard_plan->shadow_pass_count == 1 &&
             hard_plan->pass_keys.front() == "shadow_cast",
         "hard shadow one cast pass");

  // Golden: hard shadow under a red rect; content order preserved for two
  // casters (red then green).
  {
    ShadowScene scene;
    scene.width_px = 32;
    scene.height_px = 32;
    scene.clear_color = {255, 255, 255, 255};
    ShadowCaster a;
    a.shape = DrawOp{.kind = DrawOpKind::fill_rect,
                     .rect = {8, 8, 8, 8},
                     .color = {255, 0, 0, 255}};
    a.shadow.enabled = true;
    a.shadow.offset_x_px = 4;
    a.shadow.offset_y_px = 4;
    a.shadow.soft_radius_px = 0;
    a.shadow.color = {0, 0, 0, 255};
    ShadowCaster b;
    b.shape = DrawOp{.kind = DrawOpKind::fill_rect,
                     .rect = {18, 8, 6, 6},
                     .color = {0, 255, 0, 255}};
    b.shadow.enabled = false;
    scene.casters = {a, b};

    auto img = rasterize_shadow_scene(scene);
    expect(img.has_value(), "hard golden");
    if (img) {
      expect(rgb_eq(at(*img, 10, 10), 255, 0, 0), "red content");
      // Shadow-only: offset silhouette outside the red rect.
      expect(rgb_eq(at(*img, 18, 18), 0, 0, 0), "hard shadow pixel");
      expect(rgb_eq(at(*img, 20, 10), 0, 255, 0), "green content after red");
      expect(rgb_eq(at(*img, 0, 0), 255, 255, 255), "clear untouched");
    }
  }

  // Soft shadow golden: shadow region near offset is darkened but soft edge
  // is not binary; content pixel stays red.
  {
    ShadowScene scene;
    scene.width_px = 32;
    scene.height_px = 32;
    scene.clear_color = {255, 255, 255, 255};
    ShadowCaster a;
    a.shape = DrawOp{.kind = DrawOpKind::fill_rect,
                     .rect = {10, 10, 8, 8},
                     .color = {255, 0, 0, 255}};
    a.shadow.enabled = true;
    a.shadow.offset_x_px = 3;
    a.shadow.offset_y_px = 3;
    a.shadow.soft_radius_px = 2;
    a.shadow.quality = ShadowQualityTier::medium;
    a.shadow.color = {0, 0, 0, 180};
    scene.casters = {a};

    auto img = rasterize_shadow_scene(scene);
    expect(img.has_value(), "soft golden");
    if (img) {
      expect(rgb_eq(at(*img, 12, 12), 255, 0, 0), "content unchanged");
      // Soft shadow only: beyond the red rect, near the offset silhouette.
      const auto shadow = at(*img, 19, 19);
      expect(shadow.r < 250 && shadow.g < 250 && shadow.b < 250,
             "soft shadow darkens");
      // Halo outside the hard offset rect [13,21) after blur.
      const auto halo = at(*img, 22, 22);
      expect(halo.r < 255 || halo.g < 255 || halo.b < 255, "soft halo exists");
      expect(!(halo.r == 0 && halo.g == 0 && halo.b == 0),
             "soft halo not hard black");
    }
  }

  // Disabled shadow caster adds no shadow pixels.
  {
    ShadowScene scene;
    scene.width_px = 16;
    scene.height_px = 16;
    scene.clear_color = {10, 10, 10, 255};
    ShadowCaster a;
    a.shape = DrawOp{.kind = DrawOpKind::fill_rect,
                     .rect = {2, 2, 4, 4},
                     .color = {0, 0, 255, 255}};
    a.shadow.enabled = false;
    a.shadow.offset_x_px = 4;
    a.shadow.offset_y_px = 4;
    a.shadow.color = {0, 0, 0, 255};
    scene.casters = {a};
    auto img = rasterize_shadow_scene(scene);
    expect(img.has_value(), "disabled shadow");
    if (img) {
      expect(rgb_eq(at(*img, 8, 8), 10, 10, 10), "no shadow when disabled");
      expect(rgb_eq(at(*img, 3, 3), 0, 0, 255), "content present");
    }
  }

  // Reject bad params.
  {
    ShadowScene bad;
    bad.width_px = 8;
    bad.height_px = 8;
    ShadowCaster a;
    a.shape = DrawOp{.kind = DrawOpKind::fill_rect,
                     .rect = {1, 1, 2, 2},
                     .color = {1, 2, 3, 255}};
    a.shadow.enabled = true;
    a.shadow.soft_radius_px = -1.0f;
    bad.casters = {a};
    expect(!validate_shadow_scene(bad).has_value(), "reject negative radius");
  }

  expect(shadow_blur_iterations(ShadowQualityTier::low) == 1, "tier low");
  expect(shadow_blur_iterations(ShadowQualityTier::high) == 3, "tier high");
  expect(shadow_blur_radius_px(100.0f, ShadowQualityTier::low) == 2.0f,
         "tier clamps radius");

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "shadow_test ok\n";
  return EXIT_SUCCESS;
}
