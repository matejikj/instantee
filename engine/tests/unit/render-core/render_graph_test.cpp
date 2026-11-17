#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/render_graph.hpp>
#include <instantee/render_core/renderer.hpp>

#include <cstdlib>
#include <iostream>
#include <utility>

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

[[nodiscard]] instantee::render_core::PassDesc
make_clear_fill(std::uint64_t target, instantee::render_core::Rgba8 clear,
                instantee::render_core::Rgba8 fill, float x, float y, float w,
                float h) {
  using namespace instantee::render_core;
  PassDesc pass{};
  pass.width_px = 8;
  pass.height_px = 8;
  pass.color_target = target;
  pass.ops.push_back(DrawOp{.kind = DrawOpKind::clear, .color = clear});
  pass.ops.push_back(DrawOp{
      .kind = DrawOpKind::fill_rect,
      .rect = Rect2f{.x = x, .y = y, .width = w, .height = h},
      .color = fill,
  });
  return pass;
}

[[nodiscard]] instantee::render_core::Rgba8
pixel(const instantee::render_core::Rgba8Image &image, std::uint32_t x,
      std::uint32_t y) {
  const std::size_t idx =
      (static_cast<std::size_t>(y) * image.width_px + x) * 4U;
  return instantee::render_core::Rgba8{
      .r = image.rgba[idx],
      .g = image.rgba[idx + 1],
      .b = image.rgba[idx + 2],
      .a = image.rgba[idx + 3],
  };
}

} // namespace

int main() {
  using namespace instantee::render_core;
  using namespace instantee::diagnostics;

  // Invalid: cycle between two passes.
  {
    RenderGraphDesc cycle{};
    cycle.output_resource = 1;
    GraphPassNode a{};
    a.pass = make_clear_fill(1, Rgba8{0, 0, 0, 255}, Rgba8{255, 0, 0, 255}, 0,
                             0, 2, 2);
    a.resources.push_back(
        GraphResourceEdge{.resource = 2, .usage = ResourceUsage::sample});
    a.resources.push_back(
        GraphResourceEdge{.resource = 1, .usage = ResourceUsage::color_write});
    GraphPassNode b{};
    b.pass = make_clear_fill(2, Rgba8{0, 0, 0, 255}, Rgba8{0, 255, 0, 255}, 0,
                             0, 2, 2);
    b.resources.push_back(
        GraphResourceEdge{.resource = 1, .usage = ResourceUsage::sample});
    b.resources.push_back(
        GraphResourceEdge{.resource = 2, .usage = ResourceUsage::color_write});
    cycle.passes.push_back(std::move(a));
    cycle.passes.push_back(std::move(b));

    FakeRenderer renderer;
    auto compiled = compile_and_execute_render_graph(renderer, cycle);
    expect(!compiled.has_value(), "cycle rejected");
    if (!compiled) {
      expect(compiled.error().code == "render_graph_cycle", "cycle code");
    }
    expect(renderer.submit_count() == 0, "cycle never submits");
  }

  // Invalid: sample without writer or import.
  {
    RenderGraphDesc orphan{};
    orphan.output_resource = 1;
    GraphPassNode node{};
    node.pass = make_clear_fill(1, Rgba8{0, 0, 0, 255}, Rgba8{1, 2, 3, 255}, 1,
                                1, 1, 1);
    node.resources.push_back(
        GraphResourceEdge{.resource = 99, .usage = ResourceUsage::sample});
    orphan.passes.push_back(std::move(node));
    FakeRenderer renderer;
    auto compiled = compile_and_execute_render_graph(renderer, orphan);
    expect(!compiled.has_value(), "unproduced sample rejected");
    if (!compiled) {
      expect(compiled.error().code == "unproduced_sample", "sample code");
    }
    expect(renderer.submit_count() == 0, "invalid sample never submits");
  }

  // Invalid: ambiguous WAW.
  {
    RenderGraphDesc waw{};
    waw.output_resource = 1;
    GraphPassNode first{};
    first.pass = make_clear_fill(1, Rgba8{0, 0, 0, 255}, Rgba8{255, 0, 0, 255},
                                 0, 0, 1, 1);
    GraphPassNode second{};
    second.pass = make_clear_fill(1, Rgba8{0, 0, 0, 255}, Rgba8{0, 255, 0, 255},
                                  0, 0, 1, 1);
    waw.passes.push_back(std::move(first));
    waw.passes.push_back(std::move(second));
    auto compiled = compile_render_graph(waw);
    expect(!compiled.has_value(), "WAW rejected");
    if (!compiled) {
      expect(compiled.error().code == "ambiguous_resource_write", "WAW code");
    }
  }

  // Valid single-pass graph: expected image pixels.
  {
    RenderGraphDesc single{};
    single.output_resource = 7;
    GraphPassNode node{};
    node.pass = make_clear_fill(7, Rgba8{10, 20, 30, 255},
                                Rgba8{200, 10, 10, 255}, 2, 3, 2, 2);
    single.passes.push_back(std::move(node));

    FakeRenderer renderer;
    auto compiled = compile_and_execute_render_graph(renderer, single);
    expect(compiled.has_value(), "single-pass compiles");
    expect(renderer.submit_count() == 1, "single submit");
    expect(renderer.submitted_passes().size() == 1, "history size");

    auto image = rasterize_render_graph(single, *compiled);
    expect(image.has_value(), "rasterize single");
    if (image) {
      expect(image->width_px == 8 && image->height_px == 8, "extent");
      const Rgba8 bg = pixel(*image, 0, 0);
      expect(bg.r == 10 && bg.g == 20 && bg.b == 30 && bg.a == 255, "clear bg");
      const Rgba8 fg = pixel(*image, 2, 3);
      expect(fg.r == 200 && fg.g == 10 && fg.b == 10 && fg.a == 255, "fill");
      const Rgba8 outside = pixel(*image, 4, 3);
      expect(outside.r == 10 && outside.g == 20 && outside.b == 30, "outside");
    }
  }

  // Valid two-pass DAG: unused pass eliminated; producer→consumer order;
  // barrier recorded; output image from consumer only.
  {
    constexpr std::uint64_t kOffscreen = 11;
    constexpr std::uint64_t kOutput = 12;

    RenderGraphDesc dag{};
    dag.output_resource = kOutput;

    GraphPassNode dead{};
    dead.pass = make_clear_fill(99, Rgba8{1, 1, 1, 255}, Rgba8{2, 2, 2, 255}, 0,
                                0, 1, 1);

    GraphPassNode producer{};
    producer.pass = make_clear_fill(kOffscreen, Rgba8{0, 0, 0, 255},
                                    Rgba8{255, 255, 0, 255}, 0, 0, 4, 4);
    producer.resources.push_back(GraphResourceEdge{
        .resource = kOffscreen, .usage = ResourceUsage::color_write});

    GraphPassNode consumer{};
    consumer.pass = make_clear_fill(kOutput, Rgba8{0, 0, 50, 255},
                                    Rgba8{0, 180, 0, 255}, 1, 1, 3, 3);
    consumer.resources.push_back(GraphResourceEdge{
        .resource = kOffscreen, .usage = ResourceUsage::sample});
    consumer.resources.push_back(GraphResourceEdge{
        .resource = kOutput, .usage = ResourceUsage::color_write});

    // Intentionally unordered declaration: consumer before producer, dead
    // first.
    dag.passes.push_back(std::move(dead));
    dag.passes.push_back(std::move(consumer));
    dag.passes.push_back(std::move(producer));

    FakeRenderer renderer;
    auto compiled = compile_and_execute_render_graph(renderer, dag);
    expect(compiled.has_value(), "dag compiles");
    if (compiled) {
      expect(compiled->pass_order.size() == 2, "dead pass eliminated");
      expect(compiled->pass_order[0] == 2, "producer first (original index 2)");
      expect(compiled->pass_order[1] == 1,
             "consumer second (original index 1)");
      expect(!compiled->barriers.empty(), "write-to-sample barrier");
      if (!compiled->barriers.empty()) {
        expect(compiled->barriers[0].resource == kOffscreen,
               "barrier resource");
        expect(compiled->barriers[0].kind == BarrierKind::color_write_to_sample,
               "barrier kind");
        expect(compiled->barriers[0].after_order_index == 0, "after producer");
        expect(compiled->barriers[0].before_order_index == 1,
               "before consumer");
      }
    }
    expect(renderer.submit_count() == 2, "two live submits");
    if (renderer.submitted_passes().size() == 2) {
      expect(renderer.submitted_passes()[0].color_target == kOffscreen,
             "submit order producer");
      expect(renderer.submitted_passes()[1].color_target == kOutput,
             "submit order consumer");
    }

    auto image = rasterize_render_graph(dag, *compiled);
    expect(image.has_value(), "rasterize dag output");
    if (image) {
      const Rgba8 bg = pixel(*image, 0, 0);
      expect(bg.r == 0 && bg.g == 0 && bg.b == 50, "consumer clear");
      const Rgba8 fg = pixel(*image, 1, 1);
      expect(fg.r == 0 && fg.g == 180 && fg.b == 0, "consumer fill");
    }
  }

  // Imported sample is allowed without a writer.
  {
    RenderGraphDesc imported{};
    imported.output_resource = 5;
    imported.imported_resources.push_back(42);
    GraphPassNode node{};
    node.pass = make_clear_fill(5, Rgba8{9, 9, 9, 255}, Rgba8{8, 8, 8, 255}, 0,
                                0, 1, 1);
    node.resources.push_back(
        GraphResourceEdge{.resource = 42, .usage = ResourceUsage::sample});
    imported.passes.push_back(std::move(node));
    auto compiled = compile_render_graph(imported);
    expect(compiled.has_value(), "imported sample ok");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
