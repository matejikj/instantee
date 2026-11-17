#include <instantee/render_core/instance_batch.hpp>
#include <instantee/render_core/primitives.hpp>

#include <cstdlib>
#include <iostream>
#include <vector>

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

[[nodiscard]] bool images_equal(const instantee::render_core::Rgba8Image &a,
                                const instantee::render_core::Rgba8Image &b) {
  return a.width_px == b.width_px && a.height_px == b.height_px &&
         a.rgba == b.rgba;
}

} // namespace

int main() {
  using namespace instantee::render_core;

  // Many identical fill_rects coalesce to one draw call; pixels match naive.
  {
    PassDesc pass;
    pass.width_px = 64;
    pass.height_px = 64;
    pass.color_target = 1;
    pass.ops.push_back(
        DrawOp{.kind = DrawOpKind::clear, .color = {0, 0, 0, 255}});
    for (int i = 0; i < 32; ++i) {
      pass.ops.push_back(DrawOp{
          .kind = DrawOpKind::fill_rect,
          .rect = {static_cast<float>(i), static_cast<float>(i), 4, 4},
          .color = {255, 0, 0, 255},
      });
    }

    auto batched = batch_pass(pass);
    expect(batched.has_value(), "batch identical rects");
    expect(batched->stats.source_ops == 32, "source op count");
    expect(batched->stats.draw_calls == 1, "one draw call for identical batch");
    expect(batched->batches.size() == 1 &&
               batched->batches.front().instances.size() == 32,
           "single instance buffer");

    Rgba8Image naive;
    Rgba8Image coalesced;
    expect(rasterize_pass(naive, pass).has_value(), "naive rasterize");
    expect(rasterize_batched_pass(coalesced, *batched).has_value(),
           "batched rasterize");
    expect(images_equal(naive, coalesced), "pixels unchanged by batching");
    expect(pixel(coalesced, 0, 0).r == 255, "first instance visible");
    expect(pixel(coalesced, 31, 31).r == 255, "last instance visible");
  }

  // Texture binding change splits batches; order preserved.
  {
    PassDesc pass;
    pass.width_px = 16;
    pass.height_px = 16;
    pass.color_target = 1;
    pass.ops.push_back(
        DrawOp{.kind = DrawOpKind::clear, .color = {0, 0, 0, 255}});
    pass.ops.push_back(DrawOp{.kind = DrawOpKind::fill_rect,
                              .rect = {1, 1, 2, 2},
                              .color = {255, 0, 0, 255},
                              .texture_binding = 1});
    pass.ops.push_back(DrawOp{.kind = DrawOpKind::fill_rect,
                              .rect = {4, 1, 2, 2},
                              .color = {0, 255, 0, 255},
                              .texture_binding = 1});
    pass.ops.push_back(DrawOp{.kind = DrawOpKind::fill_rect,
                              .rect = {7, 1, 2, 2},
                              .color = {0, 0, 255, 255},
                              .texture_binding = 2});
    pass.ops.push_back(DrawOp{.kind = DrawOpKind::fill_ellipse,
                              .rect = {10, 1, 4, 4},
                              .color = {255, 255, 0, 255},
                              .texture_binding = 2});

    auto batched = batch_pass(pass);
    expect(batched.has_value(), "batch with texture splits");
    expect(batched->stats.source_ops == 4, "four shapes");
    expect(batched->stats.draw_calls == 2, "two texture batches");
    expect(batched->stats.batch_splits == 1, "one texture barrier");
    expect(batched->batches[0].instances.size() == 2, "first binding batch");
    expect(batched->batches[1].instances.size() == 2, "second binding batch");

    Rgba8Image naive;
    Rgba8Image coalesced;
    expect(rasterize_pass(naive, pass).has_value(), "naive split rasterize");
    expect(rasterize_batched_pass(coalesced, *batched).has_value(),
           "batched split rasterize");
    expect(images_equal(naive, coalesced), "split batch pixels match");
  }

  // Mixed kinds still share one draw when texture binding matches.
  {
    PassDesc pass;
    pass.width_px = 32;
    pass.height_px = 32;
    pass.color_target = 1;
    pass.ops.push_back(
        DrawOp{.kind = DrawOpKind::clear, .color = {10, 10, 10, 255}});
    pass.ops.push_back(DrawOp{.kind = DrawOpKind::fill_rect,
                              .rect = {2, 2, 6, 6},
                              .color = {255, 0, 0, 128}});
    pass.ops.push_back(DrawOp{.kind = DrawOpKind::fill_ellipse,
                              .rect = {8, 2, 6, 6},
                              .color = {0, 255, 0, 128}});
    pass.ops.push_back(DrawOp{.kind = DrawOpKind::stroke_line,
                              .rect = {.x = 1, .y = 20},
                              .x1 = 30,
                              .y1 = 20,
                              .color = {0, 0, 255, 255},
                              .stroke_width = 2});
    auto batched = batch_pass(pass);
    expect(batched.has_value() && batched->stats.draw_calls == 1,
           "mixed kinds one draw");
    Rgba8Image a;
    Rgba8Image b;
    expect(rasterize_pass(a, pass).has_value(), "mixed naive");
    expect(rasterize_batched_pass(b, *batched).has_value(), "mixed batched");
    expect(images_equal(a, b), "mixed kind pixels match");
  }

  // Mid-pass clear rejected.
  {
    PassDesc pass;
    pass.width_px = 8;
    pass.height_px = 8;
    pass.color_target = 1;
    pass.ops.push_back(
        DrawOp{.kind = DrawOpKind::clear, .color = {0, 0, 0, 255}});
    pass.ops.push_back(DrawOp{.kind = DrawOpKind::fill_rect,
                              .rect = {1, 1, 2, 2},
                              .color = {255, 0, 0, 255}});
    pass.ops.push_back(
        DrawOp{.kind = DrawOpKind::clear, .color = {0, 0, 255, 255}});
    auto batched = batch_pass(pass);
    expect(!batched.has_value() &&
               batched.error().code == "mid_pass_clear_unsupported",
           "mid-pass clear rejected");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "instance_batch_test ok\n";
  return EXIT_SUCCESS;
}
