#include <instantee/render_core/frame_primitives.hpp>
#include <instantee/render_core/primitives.hpp>

#include <cstdlib>
#include <iostream>
#include <span>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__) ||  \
    defined(COREVIDEO_H)
#error "render-core tests must remain free of Metal/Apple headers"
#endif

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] instantee::render_core::Rgba8
pixel(const instantee::render_core::Rgba8Image &image, std::uint32_t x,
      std::uint32_t y) {
  const std::size_t idx =
      (static_cast<std::size_t>(y) * image.width_px + x) * 4U;
  return instantee::render_core::Rgba8{image.rgba[idx], image.rgba[idx + 1],
                                       image.rgba[idx + 2],
                                       image.rgba[idx + 3]};
}

} // namespace

int main() {
  using namespace instantee::render_core;

  PassDesc pass;
  pass.width_px = 32;
  pass.height_px = 32;
  pass.color_target = 1;
  pass.ops.push_back(
      DrawOp{.kind = DrawOpKind::clear, .color = {0, 0, 0, 255}});
  pass.ops.push_back(DrawOp{.kind = DrawOpKind::fill_rect,
                            .rect = {4, 4, 8, 8},
                            .color = {255, 0, 0, 255}});
  pass.ops.push_back(DrawOp{.kind = DrawOpKind::fill_ellipse,
                            .rect = {16, 4, 10, 10},
                            .color = {0, 255, 0, 255}});
  pass.ops.push_back(DrawOp{.kind = DrawOpKind::fill_rounded_rect,
                            .rect = {4, 16, 10, 10},
                            .color = {0, 0, 255, 255},
                            .corner_radius = 3});
  pass.ops.push_back(DrawOp{.kind = DrawOpKind::stroke_line,
                            .rect = {.x = 2, .y = 28},
                            .x1 = 30,
                            .y1 = 28,
                            .color = {255, 255, 0, 255},
                            .stroke_width = 2});

  Rgba8Image image;
  expect(rasterize_pass(image, pass).has_value(), "rasterize shapes");
  expect(pixel(image, 6, 6).r == 255 && pixel(image, 6, 6).g == 0, "rect fill");
  expect(pixel(image, 0, 0).r == 0 && pixel(image, 0, 0).a == 255, "clear bg");
  expect(pixel(image, 21, 9).g == 255, "ellipse center");
  expect(pixel(image, 16, 4).g == 0, "ellipse corner outside");
  expect(pixel(image, 9, 21).b == 255, "rounded center");
  expect(pixel(image, 4, 16).b == 0, "rounded sharp corner outside");
  expect(pixel(image, 16, 28).r == 255 && pixel(image, 16, 28).g == 255,
         "line mid");

  // Alpha blending: semi-transparent white over red.
  PassDesc blend;
  blend.width_px = 8;
  blend.height_px = 8;
  blend.color_target = 1;
  blend.ops.push_back(
      DrawOp{.kind = DrawOpKind::clear, .color = {255, 0, 0, 255}});
  blend.ops.push_back(DrawOp{.kind = DrawOpKind::fill_rect,
                             .rect = {0, 0, 8, 8},
                             .color = {255, 255, 255, 128}});
  Rgba8Image blended;
  expect(rasterize_pass(blended, blend).has_value(), "blend raster");
  const Rgba8 mid = pixel(blended, 3, 3);
  expect(mid.r > 200 && mid.g > 100 && mid.g < 200 && mid.b > 100 &&
             mid.b < 200,
         "src-over mid tone");

  // Lowering transforms local rect by translation.
  WorldPrimitive prim{
      .kind = DrawOpKind::fill_rect,
      .local_bounds = {0, 0, 4, 4},
      .color = {10, 20, 30, 255},
      .world_col_major = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 7, 0, 1},
  };
  auto lowered = lower_primitives_to_pass(
      std::span<const WorldPrimitive>{&prim, 1},
      LowerPrimitivesConfig{
          .width_px = 16, .height_px = 16, .color_target = 1});
  expect(lowered.has_value(), "lower ok");
  if (lowered) {
    expect(lowered->ops.size() == 2, "clear + rect");
    expect(lowered->ops[1].rect.x == 5.0f && lowered->ops[1].rect.y == 7.0f,
           "translated origin");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
