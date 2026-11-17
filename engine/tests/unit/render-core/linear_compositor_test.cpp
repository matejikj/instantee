#include <instantee/core_model/color.hpp>
#include <instantee/render_core/linear_compositor.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>

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

[[nodiscard]] bool near_u8(std::uint8_t a, std::uint8_t e, std::uint8_t tol) {
  const int d = static_cast<int>(a) - static_cast<int>(e);
  return d <= static_cast<int>(tol) && d >= -static_cast<int>(tol);
}

[[nodiscard]] std::uint8_t expected_overlay_red_channel() {
  // Red opaque clear + 50% blue overlay in linear premul:
  // dst=(1,0,0,1), src=(0,0,0.5,0.5) → out=(0.5,0,0.5,1) → sRGB(0.5)≈187
  const double encoded = instantee::core::linear_channel_to_srgb(0.5) * 255.0;
  return static_cast<std::uint8_t>(std::lround(encoded));
}

} // namespace

int main() {
  const auto expected_mid = expected_overlay_red_channel();

  // Semi-transparent overlay golden: linear composite differs from gamma
  // src-over of the same authored colors.
  {
    LinearCompositeScene scene;
    scene.width_px = 16;
    scene.height_px = 16;
    scene.clear_color = {255, 0, 0, 255};
    scene.layers.push_back(LinearLayer{
        .shape = DrawOp{.kind = DrawOpKind::fill_rect,
                        .rect = {0, 0, 16, 16},
                        .color = {0, 0, 255, 128}},
        .opacity = 1.0f,
    });

    auto linear = rasterize_linear_composite(scene);
    expect(linear.has_value(), "rasterize linear");
    auto encoded = encode_rec709_rgba8(*linear);
    expect(encoded.has_value(), "encode rec709");
    if (encoded) {
      const auto c = at(*encoded, 8, 8);
      expect(near_u8(c.r, expected_mid, kLinearCompositorGoldenTolerance),
             "linear red channel");
      expect(near_u8(c.g, 0, kLinearCompositorGoldenTolerance), "green ~0");
      expect(near_u8(c.b, expected_mid, kLinearCompositorGoldenTolerance),
             "linear blue channel");
      expect(c.a == 255, "opaque after full coverage");

      // Gamma-space wrong blend of the same inputs would be ~128 on R/B.
      expect(!near_u8(c.r, 128, 5), "not gamma-space midpoint");
    }
  }

  // Test-pattern style: opaque base + nested semi-transparent rect overlay
  // stays within a tight encode tolerance of the analytic linear result.
  {
    LinearCompositeScene scene;
    scene.width_px = 32;
    scene.height_px = 32;
    scene.clear_color = {32, 64, 96, 255};
    scene.layers.push_back(LinearLayer{
        .shape = DrawOp{.kind = DrawOpKind::fill_ellipse,
                        .rect = {4, 4, 24, 24},
                        .color = {255, 255, 0, 160}},
        .opacity = 0.75f,
    });
    auto img = rasterize_linear_composite(scene);
    expect(img.has_value(), "pattern composite");
    auto out = encode_rec709_rgba8(*img);
    expect(out.has_value(), "pattern encode");
    if (out) {
      // Outside ellipse: clear only.
      const auto bg = at(*out, 0, 0);
      expect(near_u8(bg.r, 32, 1) && near_u8(bg.g, 64, 1) &&
                 near_u8(bg.b, 96, 1),
             "clear preserved");
      // Inside ellipse: not identical to clear (overlay applied).
      const auto mid = at(*out, 16, 16);
      expect(!(mid.r == bg.r && mid.g == bg.g && mid.b == bg.b),
             "overlay changed center");
    }
  }

  // Flatten over black for encoder handoff.
  {
    std::vector<std::uint8_t> rgba{240, 32, 64, 128};
    auto flat = flatten_rec709_rgba8_over_black(rgba);
    expect(flat.has_value(), "flatten");
    if (flat) {
      expect((*flat)[0] == 120 && (*flat)[1] == 16 && (*flat)[2] == 32 &&
                 (*flat)[3] == 255,
             "flatten oracle");
    }
  }

  // Reject bad opacity.
  {
    LinearCompositeScene bad;
    bad.width_px = 4;
    bad.height_px = 4;
    bad.layers.push_back(LinearLayer{
        .shape = DrawOp{.kind = DrawOpKind::fill_rect,
                        .rect = {0, 0, 4, 4},
                        .color = {1, 2, 3, 255}},
        .opacity = 1.5f,
    });
    expect(!validate_linear_composite_scene(bad).has_value(), "reject opacity");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "linear_compositor_test ok\n";
  return EXIT_SUCCESS;
}
