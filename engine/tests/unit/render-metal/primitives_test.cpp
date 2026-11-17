#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/instance_batch.hpp>
#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/primitive_renderer.hpp>

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] instantee::render_metal::Rgba8
at(const instantee::render_metal::Rgba8Frame &frame, std::uint32_t x,
   std::uint32_t y) {
  const std::size_t idx =
      (static_cast<std::size_t>(y) * frame.extent.width + x) * 4U;
  return instantee::render_metal::Rgba8{frame.rgba[idx], frame.rgba[idx + 1],
                                        frame.rgba[idx + 2],
                                        frame.rgba[idx + 3]};
}

} // namespace

int main() {
  using namespace instantee::render_metal;
  using namespace instantee::render_core;

  auto device = Device::open();
  expect(device.has_value(), "open device");
  if (!device) {
    return EXIT_FAILURE;
  }
  auto renderer = PrimitiveRenderer::create(*device);
  expect(renderer.has_value(), "create primitive renderer");
  if (!renderer) {
    return EXIT_FAILURE;
  }

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

  auto frame = renderer->render(pass);
  expect(frame.has_value(), "gpu render");
  if (!frame) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  expect(frame->extent.width == 32 && frame->extent.height == 32, "extent");
  expect(at(*frame, 6, 6).r == 255, "gpu rect");
  expect(at(*frame, 21, 9).g == 255, "gpu ellipse");
  expect(at(*frame, 9, 21).b == 255, "gpu rounded");
  expect(at(*frame, 16, 28).r == 255 && at(*frame, 16, 28).g == 255,
         "gpu line");

  // Instanced batch: many identical rects, one draw call on the GPU path.
  PassDesc instanced;
  instanced.width_px = 64;
  instanced.height_px = 64;
  instanced.color_target = 1;
  instanced.ops.push_back(
      DrawOp{.kind = DrawOpKind::clear, .color = {0, 0, 0, 255}});
  for (int i = 0; i < 16; ++i) {
    instanced.ops.push_back(DrawOp{
        .kind = DrawOpKind::fill_rect,
        .rect = {static_cast<float>(2 + i * 3), 8, 2, 2},
        .color = {0, 0, 255, 255},
    });
  }
  auto batched = batch_pass(instanced);
  expect(batched.has_value() && batched->stats.draw_calls == 1,
         "gpu batch one draw");
  auto many = renderer->render(instanced);
  expect(many.has_value(), "gpu instanced render");
  if (many) {
    expect(at(*many, 2, 8).b == 255, "first instance gpu");
    expect(at(*many, 2 + 15 * 3, 8).b == 255, "last instance gpu");
    expect(at(*many, 0, 0).b == 0, "clear outside instances");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
