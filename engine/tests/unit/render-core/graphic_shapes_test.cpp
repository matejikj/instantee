#include <instantee/render_core/graphic_shapes.hpp>

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

[[nodiscard]] bool only_shared_kinds(const std::vector<DrawOp> &ops) {
  for (const auto &op : ops) {
    switch (op.kind) {
    case DrawOpKind::fill_rect:
    case DrawOpKind::fill_ellipse:
    case DrawOpKind::fill_rounded_rect:
    case DrawOpKind::stroke_line:
      break;
    case DrawOpKind::clear:
      return false;
    }
  }
  return !ops.empty();
}

[[nodiscard]] std::string snapshot_dump(std::string_view name,
                                        const GraphicShapeScene &scene,
                                        const PassDesc &pass,
                                        const Rgba8Image &img) {
  std::ostringstream ss;
  ss << "name=" << name << " w=" << scene.width_px << " h=" << scene.height_px
     << " shapes=" << scene.shapes.size() << " ops=" << pass.ops.size() << '\n';
  for (const auto &shape : scene.shapes) {
    ss << "kind=" << graphic_shape_kind_id(graphic_shape_kind(shape)) << '\n';
  }
  const std::uint32_t samples[][2] = {{2, 2},   {16, 8},  {24, 20},
                                      {40, 12}, {56, 28}, {8, 36}};
  for (const auto &s : samples) {
    const Rgba8 c = at(img, s[0], s[1]);
    ss << "p" << s[0] << "," << s[1] << "=" << static_cast<int>(c.r) << ","
       << static_cast<int>(c.g) << "," << static_cast<int>(c.b) << ","
       << static_cast<int>(c.a) << "\n";
  }
  return ss.str();
}

[[nodiscard]] bool check_snapshot(const std::filesystem::path &golden_dir,
                                  std::string_view name,
                                  const GraphicShapeScene &scene,
                                  bool write_goldens) {
  auto pass = compose_graphic_shape_pass(scene);
  expect(pass.has_value(), "compose pass");
  if (!pass) {
    return false;
  }
  // clear + lowered shared ops; never invent a new renderer path.
  expect(pass->ops.front().kind == DrawOpKind::clear, "starts with clear");
  std::vector<DrawOp> body(pass->ops.begin() + 1, pass->ops.end());
  expect(only_shared_kinds(body), "shared DrawOp kinds only");

  auto img = rasterize_graphic_shape_scene(scene);
  expect(img.has_value(), "raster scene");
  if (!img) {
    return false;
  }
  expect(img->width_px == scene.width_px && img->height_px == scene.height_px,
         "extent");

  const std::string golden = snapshot_dump(name, scene, *pass, *img);
  const auto path = golden_dir / (std::string(name) + ".txt");
  if (write_goldens) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << golden;
    expect(static_cast<bool>(out), "write golden");
    return failures == 0;
  }
  expect(write_if_missing(path, golden), "seed golden");
  expect(read_file(path) == golden, "golden match");
  return failures == 0;
}

} // namespace

int main(int argc, char **argv) {
  const bool write_goldens =
      argc == 3 && std::string_view(argv[2]) == "--write-goldens";
  if (argc != 2 && !write_goldens) {
    std::cerr << "usage: graphic_shapes_test <repo-root> [--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  const auto golden_dir = root / "contracts/fixtures/graphic-shapes/goldens";

  // Vocabulary catalog snapshot: one of each component.
  {
    GraphicShapeScene scene;
    scene.width_px = 64;
    scene.height_px = 48;
    scene.clear_color = {255, 255, 255, 255};
    scene.shapes.push_back(ArrowShape{.x0 = 4,
                                      .y0 = 40,
                                      .x1 = 28,
                                      .y1 = 16,
                                      .shaft_width = 2.0f,
                                      .head_length = 8.0f,
                                      .head_width = 7.0f,
                                      .color = {0, 0, 0, 255}});
    scene.shapes.push_back(TechnicalLineShape{.x0 = 32,
                                              .y0 = 8,
                                              .x1 = 60,
                                              .y1 = 8,
                                              .stroke_width = 1.5f,
                                              .tick_spacing = 7.0f,
                                              .tick_length = 5.0f,
                                              .color = {20, 20, 20, 255}});
    scene.shapes.push_back(
        OutlineShape{.bounds = {.x = 34, .y = 14, .width = 26, .height = 16},
                     .stroke_width = 2.0f,
                     .color = {0, 0, 0, 255}});
    scene.shapes.push_back(
        StampShape{.bounds = {.x = 4, .y = 4, .width = 18, .height = 14},
                   .corner_radius = 3.0f,
                   .ring_inset_px = 2.0f,
                   .fill = {198, 255, 0, 255},
                   .ring = {0, 0, 0, 255}});
    scene.shapes.push_back(NumericCalloutShape{.cx = 48,
                                               .cy = 36,
                                               .radius = 8.0f,
                                               .digit = 3,
                                               .stem_x = 40,
                                               .stem_y = 44,
                                               .fill = {0, 0, 0, 255},
                                               .ink = {255, 255, 255, 255},
                                               .stem_color = {0, 0, 0, 255}});
    scene.shapes.push_back(
        HighlightShape{.bounds = {.x = 8, .y = 28, .width = 20, .height = 10},
                       .corner_radius = 2.0f,
                       .color = {198, 255, 0, 120}});
    expect(check_snapshot(golden_dir, "vocabulary", scene, write_goldens),
           "vocabulary snapshot");

    // Determinism.
    auto a = rasterize_graphic_shape_scene(scene);
    auto b = rasterize_graphic_shape_scene(scene);
    expect(a.has_value() && b.has_value() && a->rgba == b->rgba,
           "deterministic");
  }

  // Arrow alone — shared stroke_line primitives only.
  {
    auto ops = lower_graphic_shape(ArrowShape{.x0 = 0,
                                              .y0 = 0,
                                              .x1 = 20,
                                              .y1 = 0,
                                              .shaft_width = 2,
                                              .head_length = 6,
                                              .head_width = 6,
                                              .color = {0, 0, 0, 255}});
    expect(ops.has_value() && ops->size() == 3, "arrow three strokes");
    expect(ops && only_shared_kinds(*ops), "arrow shared kinds");
  }

  // Digit bounds + empty scene rejection.
  {
    expect(
        !lower_graphic_shape(
             NumericCalloutShape{.cx = 10, .cy = 10, .radius = 5, .digit = 10})
             .has_value(),
        "reject digit");
    GraphicShapeScene empty;
    empty.width_px = 8;
    empty.height_px = 8;
    expect(!compose_graphic_shape_pass(empty).has_value(), "reject empty");
  }

  // Zero-length arrow rejected.
  {
    expect(!lower_graphic_shape(ArrowShape{.x0 = 1, .y0 = 1, .x1 = 1, .y1 = 1})
                .has_value(),
           "reject zero arrow");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
