#include <instantee/render_core/clip_compositor.hpp>

#include <cstdlib>
#include <iostream>

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

[[nodiscard]] bool approx_rgb(const Rgba8 &c, std::uint8_t r, std::uint8_t g,
                              std::uint8_t b, std::uint8_t tol = 0) {
  const auto near = [tol](std::uint8_t a, std::uint8_t e) {
    const int d = static_cast<int>(a) - static_cast<int>(e);
    return d <= tol && d >= -static_cast<int>(tol);
  };
  return near(c.r, r) && near(c.g, g) && near(c.b, b);
}

} // namespace

int main() {
  // Nested scissors: outer [4,4,24,24], inner [8,8,8,8]; red fill of full
  // canvas must only appear inside the intersection.
  {
    ClipScript script;
    script.width_px = 32;
    script.height_px = 32;
    script.commands.push_back(ClipCommand{
        .kind = ClipCommandKind::draw,
        .op = {.kind = DrawOpKind::clear, .color = {0, 0, 0, 255}}});
    script.commands.push_back(ClipCommand{.kind = ClipCommandKind::push_scissor,
                                          .scissor = {4, 4, 24, 24}});
    script.commands.push_back(ClipCommand{.kind = ClipCommandKind::push_scissor,
                                          .scissor = {8, 8, 8, 8}});
    script.commands.push_back(ClipCommand{.kind = ClipCommandKind::draw,
                                          .op = {.kind = DrawOpKind::fill_rect,
                                                 .rect = {0, 0, 32, 32},
                                                 .color = {255, 0, 0, 255}}});
    script.commands.push_back(ClipCommand{.kind = ClipCommandKind::pop});
    script.commands.push_back(ClipCommand{.kind = ClipCommandKind::pop});

    auto img = rasterize_clip_script(script);
    expect(img.has_value(), "nested scissor raster");
    if (img) {
      expect(approx_rgb(at(*img, 10, 10), 255, 0, 0), "inside nested scissor");
      expect(approx_rgb(at(*img, 6, 6), 0, 0, 0), "outside inner scissor");
      expect(approx_rgb(at(*img, 0, 0), 0, 0, 0), "outside outer scissor");
    }
    auto stats = clip_script_stats(script);
    expect(stats.has_value() && stats->max_depth == 2 &&
               stats->scissor_pushes == 2,
           "nested scissor stats");
  }

  // Nested shape masks: outer ellipse then inner rect mask; green only where
  // both cover.
  {
    ClipScript script;
    script.width_px = 32;
    script.height_px = 32;
    script.commands.push_back(ClipCommand{
        .kind = ClipCommandKind::draw,
        .op = {.kind = DrawOpKind::clear, .color = {0, 0, 0, 255}}});
    script.commands.push_back(
        ClipCommand{.kind = ClipCommandKind::push_shape_mask,
                    .op = {.kind = DrawOpKind::fill_ellipse,
                           .rect = {4, 4, 24, 24},
                           .color = {255, 255, 255, 255}}});
    script.commands.push_back(
        ClipCommand{.kind = ClipCommandKind::push_shape_mask,
                    .op = {.kind = DrawOpKind::fill_rect,
                           .rect = {10, 10, 12, 12},
                           .color = {255, 255, 255, 255}}});
    script.commands.push_back(ClipCommand{.kind = ClipCommandKind::draw,
                                          .op = {.kind = DrawOpKind::fill_rect,
                                                 .rect = {0, 0, 32, 32},
                                                 .color = {0, 255, 0, 255}}});
    script.commands.push_back(ClipCommand{.kind = ClipCommandKind::pop});
    script.commands.push_back(ClipCommand{.kind = ClipCommandKind::pop});

    auto img = rasterize_clip_script(script);
    expect(img.has_value(), "nested mask raster");
    if (img) {
      expect(approx_rgb(at(*img, 16, 16), 0, 255, 0), "inside both masks");
      expect(approx_rgb(at(*img, 6, 16), 0, 0, 0),
             "ellipse-only region stays clear");
      expect(approx_rgb(at(*img, 0, 0), 0, 0, 0), "outside masks");
    }
  }

  // Combined scissor + mask across layers: scissor then ellipse mask.
  {
    ClipScript script;
    script.width_px = 32;
    script.height_px = 32;
    script.commands.push_back(ClipCommand{
        .kind = ClipCommandKind::draw,
        .op = {.kind = DrawOpKind::clear, .color = {20, 20, 20, 255}}});
    script.commands.push_back(ClipCommand{.kind = ClipCommandKind::push_scissor,
                                          .scissor = {0, 0, 16, 32}});
    script.commands.push_back(
        ClipCommand{.kind = ClipCommandKind::push_shape_mask,
                    .op = {.kind = DrawOpKind::fill_ellipse,
                           .rect = {2, 8, 20, 16},
                           .color = {255, 255, 255, 255}}});
    script.commands.push_back(ClipCommand{.kind = ClipCommandKind::draw,
                                          .op = {.kind = DrawOpKind::fill_rect,
                                                 .rect = {0, 0, 32, 32},
                                                 .color = {0, 0, 255, 255}}});
    script.commands.push_back(ClipCommand{.kind = ClipCommandKind::pop});
    script.commands.push_back(ClipCommand{.kind = ClipCommandKind::pop});

    auto img = rasterize_clip_script(script);
    expect(img.has_value(), "scissor+mask raster");
    if (img) {
      expect(approx_rgb(at(*img, 8, 16), 0, 0, 255), "left masked blue");
      expect(approx_rgb(at(*img, 24, 16), 20, 20, 20),
             "right of scissor untouched");
    }
  }

  // Error paths.
  {
    ClipScript bad;
    bad.width_px = 8;
    bad.height_px = 8;
    bad.commands.push_back(ClipCommand{.kind = ClipCommandKind::push_scissor,
                                       .scissor = {0, 0, 4, 4}});
    expect(!validate_clip_script(bad).has_value(), "reject imbalance");

    ClipScript clear_bad;
    clear_bad.width_px = 8;
    clear_bad.height_px = 8;
    clear_bad.commands.push_back(ClipCommand{
        .kind = ClipCommandKind::push_scissor, .scissor = {0, 0, 4, 4}});
    clear_bad.commands.push_back(ClipCommand{
        .kind = ClipCommandKind::draw,
        .op = {.kind = DrawOpKind::clear, .color = {0, 0, 0, 255}}});
    clear_bad.commands.push_back(ClipCommand{.kind = ClipCommandKind::pop});
    expect(!validate_clip_script(clear_bad).has_value(),
           "reject clear under clip");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "clip_compositor_test ok\n";
  return EXIT_SUCCESS;
}
