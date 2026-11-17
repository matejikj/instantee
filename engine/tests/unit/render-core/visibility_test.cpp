#include <instantee/render_core/primitives.hpp>
#include <instantee/render_core/visibility.hpp>

#include <cstdlib>
#include <iostream>
#include <vector>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__) ||  \
    defined(COREVIDEO_H)
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

[[nodiscard]] bool images_equal(const Rgba8Image &a, const Rgba8Image &b) {
  return a.width_px == b.width_px && a.height_px == b.height_px &&
         a.rgba == b.rgba;
}

} // namespace

int main() {
  const Viewport2f viewport{0, 0, 32, 32};

  // Offscreen candidates produce no draws; counters report the drop.
  {
    std::vector<SceneDrawItem> items;
    items.push_back(SceneDrawItem{
        .op = DrawOp{.kind = DrawOpKind::fill_rect,
                     .rect = {40, 40, 8, 8},
                     .color = {255, 0, 0, 255}},
        .visible = true,
        .stable_key = 1,
    });
    items.push_back(SceneDrawItem{
        .op = DrawOp{.kind = DrawOpKind::fill_rect,
                     .rect = {4, 4, 8, 8},
                     .color = {0, 255, 0, 255}},
        .visible = true,
        .stable_key = 2,
    });
    items.push_back(SceneDrawItem{
        .op = DrawOp{.kind = DrawOpKind::fill_rect,
                     .rect = {10, 10, 4, 4},
                     .color = {0, 0, 255, 255}},
        .visible = false,
        .stable_key = 3,
    });

    auto culled = cull_and_sort_draws(items, viewport);
    expect(culled.has_value(), "cull ok");
    if (culled) {
      expect(culled->stats.candidates == 3, "candidates");
      expect(culled->stats.culled_offscreen == 1, "offscreen counter");
      expect(culled->stats.culled_invisible == 1, "invisible counter");
      expect(culled->stats.kept == 1, "kept counter");
      expect(culled->draws.size() == 1, "one draw");
      expect(culled->draws[0].color.g == 255, "kept green onscreen");
    }
  }

  // Empty clip ∩ viewport culls without emitting a draw.
  {
    std::vector<SceneDrawItem> items;
    items.push_back(SceneDrawItem{
        .op = DrawOp{.kind = DrawOpKind::fill_rect,
                     .rect = {0, 0, 16, 16},
                     .color = {255, 255, 255, 255}},
        .stable_key = 1,
        .clip = Rect2f{100, 100, 10, 10},
    });
    auto culled = cull_and_sort_draws(items, viewport);
    expect(culled.has_value(), "clip cull");
    if (culled) {
      expect(culled->stats.culled_empty_clip == 1, "empty clip counter");
      expect(culled->draws.empty(), "no draws for empty clip");
    }
  }

  // Transparent back-to-front: farther (smaller depth_z) before nearer.
  {
    std::vector<SceneDrawItem> items;
    items.push_back(SceneDrawItem{
        .op = DrawOp{.kind = DrawOpKind::fill_rect,
                     .rect = {0, 0, 8, 8},
                     .color = {255, 0, 0, 128}},
        .transparent = true,
        .layer_order = 0,
        .depth_z = 0.0,
        .stable_key = 10,
    });
    items.push_back(SceneDrawItem{
        .op = DrawOp{.kind = DrawOpKind::fill_rect,
                     .rect = {0, 0, 8, 8},
                     .color = {0, 0, 255, 128}},
        .transparent = true,
        .layer_order = 0,
        .depth_z = -1.0,
        .stable_key = 20,
    });
    auto culled = cull_and_sort_draws(items, Viewport2f{0, 0, 8, 8});
    expect(culled.has_value(), "alpha sort");
    if (culled) {
      expect(culled->draws.size() == 2, "both kept");
      expect(culled->draws[0].color.b == 255, "far blue first");
      expect(culled->draws[1].color.r == 255, "near red second");
    }
  }

  // Opaque batch precedes transparent paint.
  {
    std::vector<SceneDrawItem> items;
    items.push_back(SceneDrawItem{
        .op = DrawOp{.kind = DrawOpKind::fill_rect,
                     .rect = {0, 0, 4, 4},
                     .color = {1, 2, 3, 128}},
        .transparent = true,
        .stable_key = 1,
    });
    items.push_back(SceneDrawItem{
        .op = DrawOp{.kind = DrawOpKind::fill_rect,
                     .rect = {0, 0, 4, 4},
                     .color = {4, 5, 6, 255}},
        .transparent = false,
        .stable_key = 2,
    });
    auto culled = cull_and_sort_draws(items, Viewport2f{0, 0, 4, 4});
    expect(culled.has_value(), "opaque first");
    if (culled) {
      expect(culled->draws[0].color.r == 4, "opaque before transparent");
      expect(culled->draws[1].color.r == 1, "transparent second");
    }
  }

  // Golden: culled pass pixels match the unculled reference when dropped
  // draws were fully offscreen.
  {
    PassDesc unculled;
    unculled.width_px = 16;
    unculled.height_px = 16;
    unculled.color_target = 1;
    unculled.ops.push_back(
        DrawOp{.kind = DrawOpKind::clear, .color = {16, 16, 16, 255}});
    unculled.ops.push_back(DrawOp{.kind = DrawOpKind::fill_rect,
                                  .rect = {2, 2, 6, 6},
                                  .color = {200, 40, 40, 255}});
    unculled.ops.push_back(DrawOp{.kind = DrawOpKind::fill_rect,
                                  .rect = {40, -10, 8, 8},
                                  .color = {0, 255, 0, 255}});
    unculled.ops.push_back(DrawOp{.kind = DrawOpKind::fill_ellipse,
                                  .rect = {8, 8, 5, 5},
                                  .color = {40, 40, 200, 180}});

    CullSortStats stats{};
    auto culled = cull_offscreen_pass(unculled, &stats);
    expect(culled.has_value(), "cull pass");
    if (culled) {
      expect(stats.culled_offscreen == 1, "one offscreen shape");
      expect(stats.kept == 2, "two shapes kept");
      expect(culled->ops.size() == 3, "clear + two shapes");

      Rgba8Image ref;
      Rgba8Image out;
      expect(rasterize_pass(ref, unculled).has_value(), "raster unculled");
      expect(rasterize_pass(out, *culled).has_value(), "raster culled");
      expect(images_equal(ref, out), "culled matches unculled golden");
      expect(at(out, 4, 4).r == 200, "onscreen rect present");
    }
  }

  // Partially visible items are kept (safe cull — no false drops).
  {
    std::vector<SceneDrawItem> items;
    items.push_back(SceneDrawItem{
        .op = DrawOp{.kind = DrawOpKind::fill_rect,
                     .rect = {28, 28, 8, 8},
                     .color = {255, 255, 0, 255}},
        .stable_key = 1,
    });
    auto culled = cull_and_sort_draws(items, viewport);
    expect(culled.has_value() && culled->stats.kept == 1, "partial kept");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "visibility_test ok\n";
  return EXIT_SUCCESS;
}
