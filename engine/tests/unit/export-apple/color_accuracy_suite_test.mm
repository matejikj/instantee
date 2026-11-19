#include <instantee/core_model/color.hpp>
#include <instantee/core_model/time.hpp>
#include <instantee/export_apple/color_path.hpp>
#include <instantee/export_apple/media_conformance.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>
#include <instantee/export_apple/video_writer.hpp>
#include <instantee/preview/frame_png.hpp>
#include <instantee/render_core/linear_compositor.hpp>
#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/frame_preview.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

#if !__has_feature(objc_arc)
#error "Apple adapter tests require ARC"
#endif

namespace {

using instantee::core::FrameIndex;
using instantee::core::linear_channel_to_srgb;
using instantee::core::LinearRgba;
using instantee::preview::rasterize_preview_frame;
using instantee::render_core::DrawOp;
using instantee::render_core::DrawOpKind;
using instantee::render_core::encode_rec709_rgba8;
using instantee::render_core::LinearCompositeScene;
using instantee::render_core::LinearLayer;
using instantee::render_core::rasterize_linear_composite;
using instantee::runtime::AnimationProgram;
using instantee::runtime::ColorKeyframe;
using instantee::runtime::ColorPropertyBinding;
using instantee::runtime::CompiledSceneDraft;
using instantee::runtime::make_compiled_scene;
using instantee::runtime::preview_frame_at;
using instantee::runtime::RuntimeNode;
using instantee::runtime::RuntimeNodeKind;
using instantee::runtime::RuntimeTextRun;
using instantee::spike::apple::ColorAccuracySample;
using instantee::spike::apple::compare_preview_rgba8_to_decoded_bgra8;
using instantee::spike::apple::decode_video_frames_bgra;
using instantee::spike::apple::flatten_straight_rgba8_to_opaque_bgra8;
using instantee::spike::apple::kRec709RoundtripMaxChannelDelta;
using instantee::spike::apple::MediaConformanceExpect;
using instantee::spike::apple::PixelBufferFormat;
using instantee::spike::apple::PixelBufferPool;
using instantee::spike::apple::Rgba8Color;
using instantee::spike::apple::validate_mp4_conformance;
using instantee::spike::apple::VideoWriter;
using instantee::spike::apple::VideoWriterConfig;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::filesystem::path make_temp_dir() {
  const auto directory =
      std::filesystem::temp_directory_path() / "instantee-color-acc-XXXXXX";
  std::string template_path = directory.string();
  if (mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path};
}

[[nodiscard]] int encode_rgba_frame(const std::filesystem::path &output,
                                    std::uint32_t width, std::uint32_t height,
                                    const std::vector<std::uint8_t> &rgba) {
  PixelBufferFormat format;
  format.width = width;
  format.height = height;
  auto pool = PixelBufferPool::create(format, 2);
  if (!pool) {
    return 1;
  }
  VideoWriterConfig config;
  config.output_path = output;
  config.format = format;
  auto writer = VideoWriter::create(config);
  if (!writer) {
    return 2;
  }
  std::vector<std::uint8_t> bgra(rgba.size());
  if (!flatten_straight_rgba8_to_opaque_bgra8(rgba, bgra)) {
    return 3;
  }
  auto buffer = pool->acquire();
  if (!buffer || !buffer->fill_bgra8(bgra) ||
      !writer->append(*buffer, FrameIndex{0}) || !writer->finish()) {
    return 4;
  }
  return 0;
}

[[nodiscard]] int roundtrip_rgba(const char *label,
                                 const std::filesystem::path &directory,
                                 std::uint32_t width, std::uint32_t height,
                                 const std::vector<std::uint8_t> &rgba,
                                 std::span<const ColorAccuracySample> samples) {
  const auto output = directory / (std::string(label) + ".mp4");
  if (const int code = encode_rgba_frame(output, width, height, rgba);
      code != 0) {
    std::cerr << "FAIL: encode " << label << " code=" << code << '\n';
    ++failures;
    return code;
  }

  // Metadata gate uses the same profile flags as production; geometry may be
  // a reduced fixture size, so skip the V1 1080×1920 expect here.
  auto frames = decode_video_frames_bgra(output);
  expect(frames.has_value() && frames->size() == 1U, "decode one frame");
  if (!frames || frames->empty()) {
    return 10;
  }

  auto stats = compare_preview_rgba8_to_decoded_bgra8(
      rgba, width, height, (*frames)[0], samples,
      kRec709RoundtripMaxChannelDelta);
  expect(stats.has_value(), label);
  if (!stats) {
    std::cerr << "  code=" << stats.error().code
              << " msg=" << stats.error().message << '\n';
    return 11;
  }
  expect(stats->samples_checked == samples.size(), "sample count");
  expect(stats->max_channel_delta <= kRec709RoundtripMaxChannelDelta,
         "within defined tolerance");
  return 0;
}

[[nodiscard]] std::vector<std::uint8_t>
make_solid(std::uint32_t width, std::uint32_t height, Rgba8Color color) {
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4U);
  for (std::size_t i = 0; i < rgba.size(); i += 4U) {
    rgba[i] = color.r;
    rgba[i + 1U] = color.g;
    rgba[i + 2U] = color.b;
    rgba[i + 3U] = color.a;
  }
  return rgba;
}

[[nodiscard]] std::vector<std::uint8_t>
make_horizontal_gradient(std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4U);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4U;
      const auto tone = static_cast<std::uint8_t>(
          (static_cast<std::uint32_t>(x) * 255U) / (width - 1U));
      rgba[i] = tone;
      rgba[i + 1U] = tone;
      rgba[i + 2U] = tone;
      rgba[i + 3U] = 255;
    }
  }
  return rgba;
}

// 4 px vertical stems — fine enough to stress chroma, coarse enough that bar
// centers survive typical H.264 softening within the documented delta.
[[nodiscard]] std::vector<std::uint8_t>
make_fine_text_like(std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4U,
                                 0);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const bool stem = ((x / 4U) % 2U) == 0U;
      const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4U;
      const std::uint8_t tone = stem ? 240 : 16;
      rgba[i] = tone;
      rgba[i + 1U] = tone;
      rgba[i + 2U] = tone;
      rgba[i + 3U] = 255;
    }
  }
  return rgba;
}

[[nodiscard]] std::vector<std::uint8_t> make_alpha_overlay() {
  LinearCompositeScene scene;
  scene.width_px = 64;
  scene.height_px = 64;
  scene.clear_color = {255, 0, 0, 255};
  scene.layers.push_back(LinearLayer{
      .shape = DrawOp{.kind = DrawOpKind::fill_rect,
                      .rect = {0, 0, 64, 64},
                      .color = {0, 0, 255, 128}},
      .opacity = 1.0f,
  });
  auto linear = rasterize_linear_composite(scene);
  if (!linear) {
    return {};
  }
  auto encoded = encode_rec709_rgba8(*linear);
  if (!encoded) {
    return {};
  }
  return encoded->rgba;
}

[[nodiscard]] int check_pattern_suite(const std::filesystem::path &directory) {
  constexpr std::uint32_t kW = 128;
  constexpr std::uint32_t kH = 128;

  {
    const auto rgba = make_solid(kW, kH, Rgba8Color{220, 220, 220, 255});
    const ColorAccuracySample samples[] = {
        {kW / 2U, kH / 2U}, {0, 0}, {kW - 1U, kH - 1U}};
    if (roundtrip_rgba("brightness_gray", directory, kW, kH, rgba, samples) !=
        0) {
      return 1;
    }
  }
  {
    const auto rgba = make_solid(kW, kH, Rgba8Color{240, 32, 64, 128});
    const ColorAccuracySample samples[] = {{kW / 2U, kH / 2U}};
    if (roundtrip_rgba("alpha_flat", directory, kW, kH, rgba, samples) != 0) {
      return 2;
    }
  }
  {
    const auto rgba = make_horizontal_gradient(kW, kH);
    const ColorAccuracySample samples[] = {
        {0, kH / 2U},       {kW / 4U, kH / 2U},
        {kW / 2U, kH / 2U}, {(3U * kW) / 4U, kH / 2U},
        {kW - 1U, kH / 2U},
    };
    if (roundtrip_rgba("gradient", directory, kW, kH, rgba, samples) != 0) {
      return 3;
    }
  }
  {
    const auto rgba = make_fine_text_like(kW, kH);
    // Sample stem centers (x=2,6,...) away from edges.
    const ColorAccuracySample samples[] = {
        {2, kH / 2U}, {6, kH / 2U}, {10, kH / 2U}, {14, kH / 2U}};
    if (roundtrip_rgba("fine_text_like", directory, kW, kH, rgba, samples) !=
        0) {
      return 4;
    }
  }
  {
    const auto rgba = make_alpha_overlay();
    expect(!rgba.empty(), "alpha overlay encode");
    if (rgba.empty()) {
      return 5;
    }
    const ColorAccuracySample samples[] = {{32, 32}};
    if (roundtrip_rgba("alpha_overlay", directory, 64, 64, rgba, samples) !=
        0) {
      return 6;
    }
  }
  return 0;
}

[[nodiscard]] int
check_preview_scene_roundtrip(const std::filesystem::path &directory) {
  CompiledSceneDraft draft;
  draft.canvas_width = 96;
  draft.canvas_height = 96;
  draft.canvas_color = "rec709-sdr";
  draft.duration_frames = FrameIndex{2};
  draft.seed = 11;
  draft.node_ids = {"panel", "title"};
  RuntimeNode panel;
  panel.kind = RuntimeNodeKind::geometry;
  panel.layer_order = 1;
  panel.visible = true;
  panel.transform.translate_x = 8;
  panel.transform.translate_y = 8;
  draft.nodes.push_back(panel);
  RuntimeTextRun run;
  run.text = "Hi";
  run.font_size_px = 24;
  run.text_role = "headline";
  draft.text_runs.push_back(run);
  RuntimeNode title;
  title.kind = RuntimeNodeKind::text;
  title.layer_order = 2;
  title.visible = true;
  title.text_run_index = 0;
  title.transform.translate_x = 16;
  title.transform.translate_y = 36;
  draft.nodes.push_back(title);

  auto scene = make_compiled_scene(std::move(draft));
  expect(scene.has_value(), "compile preview scene");
  if (!scene) {
    return 20;
  }

  AnimationProgram program;
  ColorPropertyBinding panel_color;
  panel_color.node_index = 0;
  panel_color.track.keyframes = {
      ColorKeyframe{FrameIndex{0}, LinearRgba{0.15, 0.35, 0.85, 1.0}}};
  program.color_bindings.push_back(panel_color);
  ColorPropertyBinding title_color;
  title_color.node_index = 1;
  title_color.track.keyframes = {
      ColorKeyframe{FrameIndex{0}, LinearRgba{0.95, 0.95, 0.2, 1.0}}};
  program.color_bindings.push_back(title_color);

  auto preview = preview_frame_at(**scene, program, FrameIndex{0});
  expect(preview.has_value(), "preview frame");
  if (!preview) {
    return 21;
  }
  auto image = rasterize_preview_frame(**scene, preview->state);
  expect(image.has_value(), "rasterize preview");
  if (!image) {
    return 22;
  }

  const auto output = directory / "preview_scene.mp4";
  if (encode_rgba_frame(output, image->width_px, image->height_px,
                        image->rgba) != 0) {
    expect(false, "encode preview scene");
    return 23;
  }

  // Reduced canvas: still require H.264 + Rec.709 tags + silence via inspect
  // through decode path; full V1 portrait expect would reject 96×96.
  auto expect_profile =
      MediaConformanceExpect::media_v1_portrait(FrameIndex{1});
  expect_profile.width = image->width_px;
  expect_profile.height = image->height_px;
  expect(validate_mp4_conformance(output, expect_profile).has_value(),
         "preview scene conforms");

  auto frames = decode_video_frames_bgra(output);
  expect(frames.has_value() && frames->size() == 1U, "decode preview scene");
  if (!frames || frames->empty()) {
    return 24;
  }

  const ColorAccuracySample samples[] = {
      {48, 48}, // panel body
      {40, 48}, // near title proxy
      {8, 8},   // panel corner
  };
  auto stats = compare_preview_rgba8_to_decoded_bgra8(
      image->rgba, image->width_px, image->height_px, (*frames)[0], samples,
      kRec709RoundtripMaxChannelDelta);
  expect(stats.has_value(), "preview vs decoded within tolerance");
  if (!stats) {
    std::cerr << "  code=" << stats.error().code
              << " msg=" << stats.error().message << '\n';
    return 25;
  }
  return 0;
}

[[nodiscard]] int
check_tolerance_fails_closed(const std::filesystem::path &directory) {
  constexpr std::uint32_t kW = 64;
  constexpr std::uint32_t kH = 64;
  const auto rgba = make_solid(kW, kH, Rgba8Color{255, 0, 0, 255});
  const auto output = directory / "strict.mp4";
  if (encode_rgba_frame(output, kW, kH, rgba) != 0) {
    return 30;
  }
  auto frames = decode_video_frames_bgra(output);
  if (!frames || frames->empty()) {
    return 31;
  }
  const ColorAccuracySample samples[] = {{kW / 2U, kH / 2U}};
  // Zero tolerance is stricter than H.264 roundtrip allows on saturated red.
  auto strict = compare_preview_rgba8_to_decoded_bgra8(
      rgba, kW, kH, (*frames)[0], samples, /*max_channel_delta=*/0);
  expect(!strict.has_value() &&
             strict.error().code == "color_accuracy.tolerance",
         "strict tolerance rejects lossy roundtrip");
  return 0;
}

} // namespace

int main() {
  @autoreleasepool {
    const auto directory = make_temp_dir();
    if (directory.empty()) {
      return EXIT_FAILURE;
    }

    expect(check_pattern_suite(directory) == 0, "pattern suite");
    expect(check_preview_scene_roundtrip(directory) == 0, "preview scene");
    expect(check_tolerance_fails_closed(directory) == 0, "fail closed");

    // Sanity: sRGB transfer mid-gray is a defined brightness anchor.
    const auto mid = static_cast<std::uint8_t>(
        std::lround(linear_channel_to_srgb(0.5) * 255.0));
    expect(mid > 160 && mid < 200, "srgb mid brightness band");

    std::error_code ec;
    std::filesystem::remove_all(directory, ec);

    if (failures != 0) {
      std::cerr << failures << " assertion(s) failed\n";
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }
}
