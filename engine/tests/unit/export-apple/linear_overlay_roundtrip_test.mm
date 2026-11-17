#include <instantee/core_model/color.hpp>
#include <instantee/core_model/time.hpp>
#include <instantee/export_apple/color_path.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>
#include <instantee/export_apple/video_writer.hpp>
#include <instantee/render_core/linear_compositor.hpp>

#include <cmath>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

#if !__has_feature(objc_arc)
#error "Apple adapter tests require ARC"
#endif

namespace {

using instantee::core::FrameIndex;
using instantee::core::linear_channel_to_srgb;
using instantee::render_core::DrawOp;
using instantee::render_core::DrawOpKind;
using instantee::render_core::encode_rec709_rgba8;
using instantee::render_core::kLinearCompositorGoldenTolerance;
using instantee::render_core::LinearCompositeScene;
using instantee::render_core::LinearLayer;
using instantee::render_core::rasterize_linear_composite;
using instantee::spike::apple::Bgra8Color;
using instantee::spike::apple::bgra_within_tolerance;
using instantee::spike::apple::decode_video_frames_bgra;
using instantee::spike::apple::flatten_straight_rgba8_to_opaque_bgra8;
using instantee::spike::apple::inspect_media;
using instantee::spike::apple::kRec709RoundtripMaxChannelDelta;
using instantee::spike::apple::PixelBufferFormat;
using instantee::spike::apple::PixelBufferPool;
using instantee::spike::apple::VideoWriter;
using instantee::spike::apple::VideoWriterConfig;

[[nodiscard]] std::filesystem::path make_temp_mp4(const char *name) {
  const auto directory =
      std::filesystem::temp_directory_path() / "instantee-linear-XXXXXX";
  std::string template_path = directory.string();
  if (mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path} / name;
}

[[nodiscard]] std::uint8_t expected_mid_channel() {
  return static_cast<std::uint8_t>(
      std::lround(linear_channel_to_srgb(0.5) * 255.0));
}

} // namespace

int main() {
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
    return 1;
  }
  auto encoded = encode_rec709_rgba8(*linear);
  if (!encoded) {
    return 2;
  }
  const auto mid = expected_mid_channel();
  const std::size_t center = (static_cast<std::size_t>(32) * 64 + 32) * 4U;
  if (std::abs(static_cast<int>(encoded->rgba[center]) -
               static_cast<int>(mid)) > kLinearCompositorGoldenTolerance ||
      std::abs(static_cast<int>(encoded->rgba[center + 2]) -
               static_cast<int>(mid)) > kLinearCompositorGoldenTolerance) {
    return 3;
  }

  const auto output = make_temp_mp4("linear-overlay.mp4");
  if (output.empty()) {
    return 4;
  }

  // Small square format for a fast encode; still tagged Rec.709 by the writer.
  PixelBufferFormat format;
  format.width = 64;
  format.height = 64;
  auto pool = PixelBufferPool::create(format, 2);
  if (!pool) {
    return 5;
  }
  VideoWriterConfig config;
  config.output_path = output;
  config.format = format;
  auto writer = VideoWriter::create(config);
  if (!writer) {
    return 6;
  }

  std::vector<std::uint8_t> bgra(encoded->rgba.size());
  if (!flatten_straight_rgba8_to_opaque_bgra8(encoded->rgba, bgra)) {
    return 7;
  }
  auto buffer = pool->acquire();
  if (!buffer || !buffer->fill_bgra8(bgra)) {
    return 8;
  }
  if (!writer->append(*buffer, FrameIndex{0}) || !writer->finish()) {
    return 9;
  }

  auto info = inspect_media(output);
  if (!info || !info->playable || !info->h264 || !info->rec709_primaries ||
      !info->rec709_transfer || !info->rec709_ycbcr_matrix) {
    return 10;
  }

  auto frames = decode_video_frames_bgra(output);
  if (!frames || frames->empty()) {
    return 11;
  }
  const auto &frame = (*frames)[0];
  const std::size_t idx =
      (static_cast<std::size_t>(32) * frame.format.width + 32) * 4U;
  const Bgra8Color actual{frame.bgra[idx], frame.bgra[idx + 1],
                          frame.bgra[idx + 2], frame.bgra[idx + 3]};
  // Flattened opaque Rec.709 RGB expected as BGRA.
  const Bgra8Color expected{mid, 0, mid, 255};
  if (!bgra_within_tolerance(actual, expected,
                             kRec709RoundtripMaxChannelDelta)) {
    return 12;
  }

  std::error_code ec;
  std::filesystem::remove_all(output.parent_path(), ec);
  return 0;
}
