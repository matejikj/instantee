#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/metal_glyph_atlas.hpp>
#include <instantee/typography/text_shape.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::filesystem::path font_dir() {
  return std::filesystem::path{INSTANTEE_PINNED_FONT_DIR};
}

} // namespace

int main() {
  using namespace instantee::render_metal;
  using namespace instantee::typography;

  auto device = Device::open();
  expect(device.has_value(), "open metal device");
  if (!device) {
    std::cerr << device.error().message << '\n';
    return EXIT_FAILURE;
  }

  const auto primary = font_dir() / "NotoSans-Regular.ttf";
  expect(std::filesystem::exists(primary), "primary font present");
  const std::filesystem::path stack_paths[] = {primary};
  auto fonts = FontStack::open(stack_paths);
  expect(fonts.has_value(), "open fonts");
  if (!fonts) {
    return EXIT_FAILURE;
  }

  MetalGlyphAtlasConfig cfg;
  cfg.cpu.atlas.page_width_px = 256;
  cfg.cpu.atlas.page_height_px = 256;
  cfg.cpu.atlas.padding_px = 1;
  cfg.registry_capacity = 8;
  auto atlas = MetalGlyphAtlas::create(*device, cfg);
  expect(atlas.has_value(), "create metal glyph atlas");
  if (!atlas) {
    std::cerr << atlas.error().message << '\n';
    return EXIT_FAILURE;
  }

  TextShaper shaper;
  constexpr std::string_view kText = "GPU cache";
  constexpr std::string_view kFp = "noto-regular";

  expect(atlas->begin_frame(1).has_value(), "begin 1");
  auto cold = atlas->prepare(shaper, *fonts, kFp, kText, 30.0f);
  expect(cold.has_value(), "cold prepare");
  if (!cold) {
    std::cerr << cold.error().message << '\n';
    return EXIT_FAILURE;
  }
  expect(atlas->page_handle(0).valid(), "page 0 handle after cold");
  expect(atlas->stats().upload_batches >= 1, "cold uploaded");
  expect(atlas->stats().pages_created >= 1, "page created");
  expect(atlas->end_frame(1).has_value(), "end 1");

  const auto uploads_after_cold = atlas->stats().upload_batches;
  const auto shape_after_cold = atlas->stats().cpu.shape_calls;
  const auto raster_after_cold = atlas->stats().cpu.raster_calls;

  expect(atlas->begin_frame(2).has_value(), "begin 2");
  auto warm = atlas->prepare(shaper, *fonts, kFp, kText, 30.0f);
  expect(warm.has_value() && warm->from_layout_cache, "warm layout hit");
  expect(atlas->stats().cpu.shape_calls == shape_after_cold, "no reshape");
  expect(atlas->stats().cpu.raster_calls == raster_after_cold, "no reraster");
  // Warm resident path should not restage atlas bytes.
  expect(atlas->stats().upload_batches == uploads_after_cold ||
             atlas->stats().warm_uploads_skipped >= 1,
         "warm upload skipped or unchanged");
  expect(atlas->page_handle(0).valid(), "page still valid warm");
  expect(atlas->end_frame(2).has_value(), "end 2");

  // Overlapping frames: second begin retains pages while first is live.
  expect(atlas->begin_frame(3).has_value(), "begin 3");
  expect(atlas->prepare(shaper, *fonts, kFp, kText, 30.0f).has_value(),
         "prepare 3");
  expect(atlas->begin_frame(4).has_value(), "begin 4");
  expect(atlas->prepare(shaper, *fonts, kFp, "Other", 30.0f).has_value(),
         "prepare 4");
  expect(atlas->end_frame(3).has_value(), "end 3");
  expect(atlas->end_frame(4).has_value(), "end 4");

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "metal_glyph_atlas_test ok\n";
  return EXIT_SUCCESS;
}
