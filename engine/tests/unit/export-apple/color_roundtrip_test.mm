#include <instantee/core_model/time.hpp>
#include <instantee/export_apple/color_path.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>
#include <instantee/export_apple/video_writer.hpp>
#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/frame.hpp>
#include <instantee/render_metal/test_pattern.hpp>

#include <filesystem>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#if !__has_feature(objc_arc)
#error "Apple adapter tests require ARC"
#endif

namespace {

using instantee::core::FrameIndex;
using instantee::spike::apple::Bgra8Color;
using instantee::spike::apple::bgra_within_tolerance;
using instantee::spike::apple::decode_video_frames_bgra;
using instantee::spike::apple::flatten_straight_rgba8_over_black;
using instantee::spike::apple::flatten_straight_rgba8_to_opaque_bgra8;
using instantee::spike::apple::inspect_media;
using instantee::spike::apple::kRec709RoundtripMaxChannelDelta;
using instantee::spike::apple::PixelBufferFormat;
using instantee::spike::apple::PixelBufferPool;
using instantee::spike::apple::Rgba8Color;
using instantee::spike::apple::VideoWriter;
using instantee::spike::apple::VideoWriterConfig;
using instantee::spike::metal::Device;
using instantee::spike::metal::expected_test_pattern_pixel;
using instantee::spike::metal::kTestPatternHeight;
using instantee::spike::metal::kTestPatternWidth;
using instantee::spike::metal::TestPatternRenderer;

[[nodiscard]] std::filesystem::path make_temp_mp4(const char *name) {
  const auto directory =
      std::filesystem::temp_directory_path() / "instantee-color-XXXXXX";
  std::string template_path = directory.string();
  if (mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path} / name;
}

[[nodiscard]] Bgra8Color sample_center(const std::vector<std::uint8_t> &bgra,
                                       std::uint32_t width,
                                       std::uint32_t height) {
  const std::uint32_t x = width / 2U;
  const std::uint32_t y = height / 2U;
  const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4U;
  return Bgra8Color{bgra[index], bgra[index + 1U], bgra[index + 2U],
                    bgra[index + 3U]};
}

[[nodiscard]] int check_flatten_oracle() {
  const auto flat =
      flatten_straight_rgba8_over_black(Rgba8Color{240, 32, 64, 128});
  if (flat != Bgra8Color{32, 16, 120, 255}) {
    return 1;
  }
  const auto opaque =
      flatten_straight_rgba8_over_black(Rgba8Color{10, 20, 30, 255});
  if (opaque != Bgra8Color{30, 20, 10, 255}) {
    return 2;
  }
  const auto clear =
      flatten_straight_rgba8_over_black(Rgba8Color{255, 255, 255, 0});
  if (clear != Bgra8Color{0, 0, 0, 255}) {
    return 3;
  }

  std::vector<std::uint8_t> rgba{240, 32, 64, 128};
  std::vector<std::uint8_t> bgra(4);
  if (!flatten_straight_rgba8_to_opaque_bgra8(rgba, bgra) || bgra[0] != 32 ||
      bgra[1] != 16 || bgra[2] != 120 || bgra[3] != 255) {
    return 4;
  }
  if (flatten_straight_rgba8_to_opaque_bgra8(rgba, std::span<std::uint8_t>{}) ||
      flatten_straight_rgba8_to_opaque_bgra8(rgba, std::span<std::uint8_t>{})
              .error()
              .code != "size_mismatch") {
    return 5;
  }
  return 0;
}

[[nodiscard]] int export_solid_colors(const std::filesystem::path &output,
                                      const std::vector<Rgba8Color> &colors) {
  auto pool = PixelBufferPool::create(PixelBufferFormat::media_1080x1920(), 4);
  if (!pool) {
    return 1;
  }
  VideoWriterConfig config;
  config.output_path = output;
  auto writer = VideoWriter::create(config);
  if (!writer) {
    return 2;
  }

  const auto bytes = static_cast<std::size_t>(config.format.width) *
                     static_cast<std::size_t>(config.format.height) * 4U;
  std::vector<std::uint8_t> rgba(bytes);
  std::vector<std::uint8_t> bgra(bytes);

  for (FrameIndex frame{0}; frame < FrameIndex{colors.size()}; ++frame) {
    const auto color = colors[static_cast<std::size_t>(frame.value())];
    for (std::size_t i = 0; i < bytes; i += 4U) {
      rgba[i] = color.r;
      rgba[i + 1U] = color.g;
      rgba[i + 2U] = color.b;
      rgba[i + 3U] = color.a;
    }
    if (!flatten_straight_rgba8_to_opaque_bgra8(rgba, bgra)) {
      return 3;
    }
    auto buffer = pool->acquire();
    if (!buffer || !buffer->fill_bgra8(bgra)) {
      return 4;
    }
    if (!writer->append(*buffer, frame)) {
      return 5;
    }
  }
  if (!writer->finish()) {
    return 6;
  }
  return 0;
}

[[nodiscard]] int check_solid_roundtrip() {
  const std::vector<Rgba8Color> colors{
      {255, 255, 255, 255}, {0, 0, 0, 255},   {255, 0, 0, 255},
      {0, 255, 0, 255},     {0, 0, 255, 255}, {128, 128, 128, 255},
      {240, 32, 64, 128}, // flattens to a known mid red over black
  };

  const auto output = make_temp_mp4("solids.mp4");
  if (output.empty()) {
    return 10;
  }
  const auto directory = output.parent_path();
  if (const int code = export_solid_colors(output, colors); code != 0) {
    return 10 + code;
  }

  const auto info = inspect_media(output);
  if (!info || !info->h264 || !info->rec709_primaries ||
      !info->rec709_transfer || !info->rec709_ycbcr_matrix ||
      info->width != 1080 || info->height != 1920) {
    return 20;
  }

  const auto frames = decode_video_frames_bgra(output);
  if (!frames || frames->size() != colors.size()) {
    return 21;
  }

  for (std::size_t index = 0; index < colors.size(); ++index) {
    const auto expected = flatten_straight_rgba8_over_black(colors[index]);
    const auto &frame = (*frames)[index];
    if (frame.format.width != 1080 || frame.format.height != 1920) {
      return 22;
    }
    const auto actual =
        sample_center(frame.bgra, frame.format.width, frame.format.height);
    if (!bgra_within_tolerance(actual, expected,
                               kRec709RoundtripMaxChannelDelta)) {
      return 23;
    }
  }

  std::error_code ec;
  std::filesystem::remove_all(directory, ec);
  return 0;
}

[[nodiscard]] int check_metal_pattern_roundtrip() {
  const auto output = make_temp_mp4("pattern.mp4");
  if (output.empty()) {
    return 30;
  }
  const auto directory = output.parent_path();

  auto device = Device::open();
  if (!device) {
    return 31;
  }
  auto renderer = TestPatternRenderer::create(*device);
  if (!renderer) {
    return 32;
  }
  auto rgba = renderer->render(0);
  if (!rgba || !rgba->valid_dimensions()) {
    return 33;
  }

  std::vector<std::uint8_t> bgra(rgba->rgba.size());
  if (!flatten_straight_rgba8_to_opaque_bgra8(rgba->rgba, bgra)) {
    return 34;
  }

  auto pool = PixelBufferPool::create(PixelBufferFormat::media_1080x1920(), 2);
  if (!pool) {
    return 35;
  }
  VideoWriterConfig config;
  config.output_path = output;
  auto writer = VideoWriter::create(config);
  if (!writer) {
    return 36;
  }
  auto buffer = pool->acquire();
  if (!buffer || !buffer->fill_bgra8(bgra) ||
      !writer->append(*buffer, FrameIndex{0}) || !writer->finish()) {
    return 37;
  }

  const auto info = inspect_media(output);
  if (!info || !info->rec709_primaries || !info->rec709_transfer ||
      !info->rec709_ycbcr_matrix) {
    return 38;
  }

  const auto frames = decode_video_frames_bgra(output);
  if (!frames || frames->size() != 1) {
    return 39;
  }

  // Sample a few deterministic sites; fine spatial detail may soften under
  // H.264, so keep the documented spike tolerance.
  constexpr std::pair<std::uint32_t, std::uint32_t> samples[] = {
      {0, 0},
      {kTestPatternWidth / 2U, kTestPatternHeight / 2U},
      {kTestPatternWidth - 1U, kTestPatternHeight - 1U},
      {15U, 15U},
  };
  const auto &decoded = (*frames)[0];
  for (const auto [x, y] : samples) {
    const auto pixel = expected_test_pattern_pixel(0, x, y);
    const auto expected = flatten_straight_rgba8_over_black(
        Rgba8Color{pixel.r, pixel.g, pixel.b, pixel.a});
    const std::size_t index =
        (static_cast<std::size_t>(y) * decoded.format.width + x) * 4U;
    const Bgra8Color actual{decoded.bgra[index], decoded.bgra[index + 1U],
                            decoded.bgra[index + 2U], decoded.bgra[index + 3U]};
    if (!bgra_within_tolerance(actual, expected,
                               kRec709RoundtripMaxChannelDelta)) {
      return 40;
    }
  }

  std::error_code ec;
  std::filesystem::remove_all(directory, ec);
  return 0;
}

} // namespace

int main() {
  @autoreleasepool {
    if (const int code = check_flatten_oracle(); code != 0) {
      return code;
    }
    if (const int code = check_solid_roundtrip(); code != 0) {
      return code;
    }
    return check_metal_pattern_roundtrip();
  }
}
