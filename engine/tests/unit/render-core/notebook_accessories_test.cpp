#include <instantee/core_model/color.hpp>
#include <instantee/render_core/notebook_accessories.hpp>

#include <cmath>
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

[[nodiscard]] bool near_u8(std::uint8_t a, std::uint8_t e, std::uint8_t tol) {
  const int d = static_cast<int>(a) - static_cast<int>(e);
  return d <= static_cast<int>(tol) && d >= -static_cast<int>(tol);
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
                                    std::string_view contents) {
  if (std::filesystem::exists(path)) {
    return true;
  }
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out << contents;
  return static_cast<bool>(out);
}

[[nodiscard]] bool check_golden(const std::filesystem::path &path,
                                std::string_view actual, bool write_goldens) {
  if (write_goldens) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out << actual;
    return true;
  }
  if (!write_if_missing(path, actual)) {
    return false;
  }
  return read_file(path) == actual;
}

[[nodiscard]] NotebookAccessoryScene make_catalog_scene() {
  NotebookAccessoryScene scene;
  scene.width_px = 96;
  scene.height_px = 64;
  scene.clear_color = Rgba8{250, 245, 230, 255};
  scene.base_ops.push_back(
      DrawOp{.kind = DrawOpKind::fill_rounded_rect,
             .rect = {.x = 6, .y = 6, .width = 84, .height = 52},
             .color = Rgba8{255, 252, 245, 255},
             .corner_radius = 4.0f});

  TapeStripSpec tape;
  tape.id = "tape";
  tape.bounds = Rect2f{.x = 20, .y = 4, .width = 36, .height = 10};
  tape.corner_radius = 2.0f;
  tape.color = Rgba8{255, 220, 120, 150};

  PinSpec pin;
  pin.id = "pin";
  pin.cx = 18;
  pin.cy = 14;
  pin.head_radius = 3.5f;
  pin.needle_length = 9.0f;

  UnderlineSpec underline;
  underline.id = "underline";
  underline.x0 = 28;
  underline.y0 = 48;
  underline.x1 = 78;
  underline.y1 = 48;
  underline.stroke_width = 2.0f;

  InkHighlightSpec highlight;
  highlight.id = "highlight";
  highlight.bounds = Rect2f{.x = 28, .y = 28, .width = 40, .height = 12};
  highlight.corner_radius = 2.0f;
  highlight.color = Rgba8{255, 230, 60, 96};

  MarkerStrokeSpec marker;
  marker.id = "marker";
  marker.preset = MarkerDrawPreset::felt_tip;
  marker.points = {{30, 20}, {50, 18}, {70, 22}};
  marker.progress = 1.0f;

  scene.accessories.push_back(tape);
  scene.accessories.push_back(pin);
  scene.accessories.push_back(underline);
  scene.accessories.push_back(highlight);
  scene.accessories.push_back(marker);
  return scene;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr
        << "usage: notebook_accessories_test <source_root> [--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  bool write_goldens = false;
  for (int i = 2; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--write-goldens") {
      write_goldens = true;
    }
  }
  const auto golden_dir =
      root / "contracts/fixtures/notebook-accessories/goldens";

  // Shared ink_stroke feature for marker presets (no parallel shader).
  {
    auto registry = make_notebook_accessory_shader_feature_registry();
    expect(registry.contains(std::string{kInkStrokeFeature}), "ink_stroke");
    expect(marker_draw_preset_style(MarkerDrawPreset::felt_tip)
               .uses_ink_stroke_feature,
           "felt uses ink feature");
    expect(!marker_draw_preset_style(MarkerDrawPreset::highlighter_pen)
                .uses_ink_stroke_feature,
           "highlighter wash is translucent fill stroke");
  }

  // Catalog golden: all accessory kinds via linear premul path.
  {
    auto scene = make_catalog_scene();
    auto linear = compose_notebook_accessory_scene(scene);
    expect(linear.has_value(), "compose catalog");
    auto encoded = rasterize_notebook_accessory_scene(scene);
    expect(encoded.has_value(), "raster catalog");
    if (linear && encoded) {
      expect(linear->layers.size() >= 6, "base + accessories layers");
      const std::string dump =
          format_notebook_accessory_golden("catalog", scene, *linear, *encoded);
      expect(check_golden(golden_dir / "catalog.txt", dump, write_goldens),
             "catalog golden");
    }
  }

  // Semi-transparent highlight layers correctly in linear premul (not gamma).
  {
    NotebookAccessoryScene scene;
    scene.width_px = 16;
    scene.height_px = 16;
    scene.clear_color = Rgba8{255, 0, 0, 255}; // opaque red paper
    InkHighlightSpec hi;
    hi.id = "hi";
    hi.bounds = Rect2f{.x = 0, .y = 0, .width = 16, .height = 16};
    hi.corner_radius = 0;
    hi.color = Rgba8{0, 0, 255, 128}; // 50% blue, sRGB/straight
    scene.accessories.push_back(hi);

    auto encoded = rasterize_notebook_accessory_scene(scene);
    expect(encoded.has_value(), "linear highlight encode");
    if (encoded) {
      const auto c = at(*encoded, 8, 8);
      // Same oracle as linear compositor: out linear (0.5,0,0.5) → ~187.
      const auto expected = static_cast<std::uint8_t>(
          std::lround(instantee::core::linear_channel_to_srgb(0.5) * 255.0));
      expect(near_u8(c.r, expected, kLinearCompositorGoldenTolerance),
             "linear red after sRGB import");
      expect(near_u8(c.b, expected, kLinearCompositorGoldenTolerance),
             "linear blue after sRGB import");
      expect(!near_u8(c.r, 128, 5), "not gamma-space midpoint");
    }
  }

  // Marker progress truncates polyline; order: tape under pin changes pixels.
  {
    MarkerStrokeSpec marker;
    marker.id = "m";
    marker.preset = MarkerDrawPreset::pencil_thin;
    marker.points = {{0, 0}, {100, 0}};
    marker.progress = 0.5f;
    auto layers = lower_notebook_accessory(marker);
    expect(layers.has_value() && layers->size() == 1, "one half segment");
    if (layers && !layers->empty()) {
      expect(std::fabs(layers->front().shape.x1 - 50.0f) < 1e-3f, "half x");
    }

    auto a = make_catalog_scene();
    auto b = make_catalog_scene();
    // Swap pin and tape order in b.
    std::swap(b.accessories[0], b.accessories[1]);
    auto ia = rasterize_notebook_accessory_scene(a);
    auto ib = rasterize_notebook_accessory_scene(b);
    expect(ia && ib && ia->rgba != ib->rgba,
           "layer order affects premul composite");
  }

  // Validation.
  {
    NotebookAccessoryScene bad;
    bad.width_px = 0;
    bad.height_px = 10;
    expect(!compose_notebook_accessory_scene(bad).has_value(),
           "reject zero size");
    UnderlineSpec u;
    u.id = "u";
    u.x0 = u.y0 = u.x1 = u.y1 = 0;
    expect(!lower_notebook_accessory(u).has_value(), "reject zero underline");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "notebook_accessories_test ok\n";
  return EXIT_SUCCESS;
}
