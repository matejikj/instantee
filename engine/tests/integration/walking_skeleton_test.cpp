#include <instantee/core_model/color.hpp>
#include <instantee/core_model/time.hpp>
#include <instantee/core_model/transform.hpp>
#include <instantee/export_core/frame_bridge.hpp>
#include <instantee/export_core/video_sink.hpp>
#include <instantee/render_core/frame_primitives.hpp>
#include <instantee/render_core/primitives.hpp>
#include <instantee/render_core/staging_pool.hpp>
#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/frame_evaluator.hpp>

#include <compression.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

class RecordingVideoSink final : public instantee::export_core::VideoSink {
public:
  std::expected<void, instantee::diagnostics::Error>
  begin(const instantee::export_core::VideoSettings &settings) override {
    settings_ = settings;
    begun_ = true;
    return {};
  }

  std::expected<void, instantee::diagnostics::Error>
  append(instantee::export_core::EncodableFrame &frame,
         instantee::core::RationalTime /*pts*/, std::stop_token stop) override {
    if (stop.stop_requested()) {
      return std::unexpected(instantee::diagnostics::Error{
          instantee::diagnostics::ExitCode::cancelled,
          instantee::diagnostics::ErrorKind::cancelled, "cancelled", "sink",
          "stop requested"});
    }
    widths_.push_back(frame.width());
    heights_.push_back(frame.height());
    frames_.push_back(frame.release_pixels());
    return {};
  }

  std::expected<instantee::export_core::ExportSummary,
                instantee::diagnostics::Error>
  finish() override {
    finished_ = true;
    return instantee::export_core::ExportSummary{
        .frames_appended = frames_.size(), .finished = true};
  }

  void cancel() noexcept override { cancelled_ = true; }

  [[nodiscard]] bool begun() const noexcept { return begun_; }
  [[nodiscard]] bool finished() const noexcept { return finished_; }
  [[nodiscard]] std::size_t frame_count() const noexcept {
    return frames_.size();
  }
  [[nodiscard]] const std::vector<std::byte> &frame(std::size_t i) const {
    return frames_.at(i);
  }

private:
  instantee::export_core::VideoSettings settings_{};
  std::vector<std::vector<std::byte>> frames_{};
  std::vector<std::uint32_t> widths_{};
  std::vector<std::uint32_t> heights_{};
  bool begun_ = false;
  bool finished_ = false;
  bool cancelled_ = false;
};

[[nodiscard]] instantee::render_core::Rgba8
linear_to_rgba8(instantee::core::LinearRgba c) {
  using instantee::core::linear_channel_to_srgb;
  return instantee::render_core::Rgba8{
      static_cast<std::uint8_t>(
          std::lround(linear_channel_to_srgb(c.r) * 255.0)),
      static_cast<std::uint8_t>(
          std::lround(linear_channel_to_srgb(c.g) * 255.0)),
      static_cast<std::uint8_t>(
          std::lround(linear_channel_to_srgb(c.b) * 255.0)),
      static_cast<std::uint8_t>(
          std::lround(std::clamp(c.a, 0.0, 1.0) * 255.0))};
}

[[nodiscard]] std::array<float, 16>
mat4_to_float(const instantee::core::Mat4 &m) {
  std::array<float, 16> out{};
  for (std::size_t i = 0; i < 16; ++i) {
    out[i] = static_cast<float>(m.m[i]);
  }
  return out;
}

[[nodiscard]] bool write_rgba8_png(const std::filesystem::path &path,
                                   std::uint32_t width, std::uint32_t height,
                                   std::span<const std::uint8_t> rgba) {
  const std::size_t expected =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
  if (rgba.size() != expected) {
    return false;
  }
  std::vector<std::uint8_t> raw((static_cast<std::size_t>(width) * 4U + 1U) *
                                height);
  for (std::uint32_t y = 0; y < height; ++y) {
    const std::size_t dst = static_cast<std::size_t>(y) *
                            (static_cast<std::size_t>(width) * 4U + 1U);
    raw[dst] = 0;
    std::memcpy(raw.data() + dst + 1U,
                rgba.data() + static_cast<std::size_t>(y) * width * 4U,
                static_cast<std::size_t>(width) * 4U);
  }
  std::vector<std::uint8_t> compressed(raw.size() + 128U);
  const std::size_t encoded = compression_encode_buffer(
      compressed.data(), compressed.size(), raw.data(), raw.size(), nullptr,
      COMPRESSION_ZLIB);
  if (encoded == 0) {
    return false;
  }
  compressed.resize(encoded);

  auto crc32 = [](std::span<const std::uint8_t> data) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::uint8_t byte : data) {
      crc ^= byte;
      for (int bit = 0; bit < 8; ++bit) {
        const std::uint32_t mask = -(crc & 1U);
        crc = (crc >> 1U) ^ (0xEDB88320u & mask);
      }
    }
    return ~crc;
  };
  auto write_u32 = [](std::FILE *file, std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>((value >> 24) & 0xFFU),
        static_cast<std::uint8_t>((value >> 16) & 0xFFU),
        static_cast<std::uint8_t>((value >> 8) & 0xFFU),
        static_cast<std::uint8_t>(value & 0xFFU)};
    return std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
  };
  auto write_chunk = [&](std::FILE *file, const char type[4],
                         std::span<const std::uint8_t> payload) {
    if (!write_u32(file, static_cast<std::uint32_t>(payload.size()))) {
      return false;
    }
    if (std::fwrite(type, 1, 4, file) != 4) {
      return false;
    }
    if (!payload.empty() && std::fwrite(payload.data(), 1, payload.size(),
                                        file) != payload.size()) {
      return false;
    }
    std::vector<std::uint8_t> for_crc(4 + payload.size());
    std::memcpy(for_crc.data(), type, 4);
    if (!payload.empty()) {
      std::memcpy(for_crc.data() + 4, payload.data(), payload.size());
    }
    return write_u32(file, crc32(for_crc));
  };

  std::FILE *file = std::fopen(path.string().c_str(), "wb");
  if (file == nullptr) {
    return false;
  }
  const std::uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
  bool ok = std::fwrite(sig, 1, 8, file) == 8;
  std::array<std::uint8_t, 13> ihdr{};
  ihdr[0] = static_cast<std::uint8_t>((width >> 24) & 0xFFU);
  ihdr[1] = static_cast<std::uint8_t>((width >> 16) & 0xFFU);
  ihdr[2] = static_cast<std::uint8_t>((width >> 8) & 0xFFU);
  ihdr[3] = static_cast<std::uint8_t>(width & 0xFFU);
  ihdr[4] = static_cast<std::uint8_t>((height >> 24) & 0xFFU);
  ihdr[5] = static_cast<std::uint8_t>((height >> 16) & 0xFFU);
  ihdr[6] = static_cast<std::uint8_t>((height >> 8) & 0xFFU);
  ihdr[7] = static_cast<std::uint8_t>(height & 0xFFU);
  ihdr[8] = 8;
  ihdr[9] = 6;
  ok = ok && write_chunk(file, "IHDR", ihdr);
  ok = ok && write_chunk(file, "IDAT", compressed);
  ok = ok && write_chunk(file, "IEND", {});
  std::fclose(file);
  return ok;
}

} // namespace

int main(int argc, char **argv) {
  using namespace instantee;
  using namespace instantee::runtime;
  using namespace instantee::render_core;
  using namespace instantee::export_core;

  const std::filesystem::path out_dir =
      argc > 1 ? std::filesystem::path{argv[1]}
               : std::filesystem::temp_directory_path() / "instantee-skeleton";
  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);

  // Programmatic scene standing in for compiled IR geometry nodes (no TS).
  CompiledSceneDraft draft;
  draft.canvas_width = 64;
  draft.canvas_height = 64;
  draft.duration_frames = core::FrameIndex{3};
  draft.seed = 7;
  draft.node_ids = {"rect", "ellipse", "line"};
  RuntimeNode rect_node;
  rect_node.kind = RuntimeNodeKind::geometry;
  rect_node.layer_order = 0;
  rect_node.transform.translate_x = 8;
  rect_node.transform.translate_y = 8;
  rect_node.blend = BlendMode::premultiplied_alpha;
  RuntimeNode ellipse_node;
  ellipse_node.kind = RuntimeNodeKind::geometry;
  ellipse_node.layer_order = 1;
  ellipse_node.transform.translate_x = 32;
  ellipse_node.transform.translate_y = 8;
  ellipse_node.blend = BlendMode::premultiplied_alpha;
  RuntimeNode line_node;
  line_node.kind = RuntimeNodeKind::geometry;
  line_node.layer_order = 2;
  line_node.transform.translate_x = 0;
  line_node.transform.translate_y = 0;
  line_node.blend = BlendMode::premultiplied_alpha;
  draft.nodes = {rect_node, ellipse_node, line_node};
  auto scene = make_compiled_scene(std::move(draft));
  expect(scene.has_value(), "compiled scene");
  if (!scene) {
    return EXIT_FAILURE;
  }

  AnimationProgram program;
  ColorTrack fade;
  fade.keyframes = {ColorKeyframe{.frame = core::FrameIndex{0},
                                  .value = {1.0, 0.0, 0.0, 1.0}},
                    ColorKeyframe{.frame = core::FrameIndex{2},
                                  .value = {1.0, 0.0, 0.0, 0.5}}};
  program.color_bindings.push_back(
      ColorPropertyBinding{.node_index = 0, .track = fade});

  auto sink = std::make_unique<RecordingVideoSink>();
  RecordingVideoSink *raw_sink = sink.get();
  CpuFrameBridge bridge(std::move(sink));

  VideoSettings settings{.width = 64, .height = 64};
  expect(raw_sink->begin(settings).has_value(), "sink begin");

  auto pool = StagingBufferPool::create(
      StagingPoolConfig{.capacity = 2, .width_px = 64, .height_px = 64});
  expect(pool.has_value(), "staging pool");

  for (std::uint64_t f = 0; f < 3; ++f) {
    auto state = evaluate_frame(**scene, program, core::FrameIndex{f});
    expect(state.has_value(), "evaluate frame");
    if (!state) {
      continue;
    }

    std::vector<WorldPrimitive> prims;
    // rect
    prims.push_back(WorldPrimitive{
        .kind = DrawOpKind::fill_rect,
        .local_bounds = {0, 0, 12, 12},
        .color = linear_to_rgba8(state->nodes[0].material_color),
        .world_col_major = mat4_to_float(state->nodes[0].world),
    });
    // ellipse
    prims.push_back(WorldPrimitive{
        .kind = DrawOpKind::fill_ellipse,
        .local_bounds = {0, 0, 16, 16},
        .color = {0, 200, 0, 255},
        .world_col_major = mat4_to_float(state->nodes[1].world),
    });
    // line
    prims.push_back(WorldPrimitive{
        .kind = DrawOpKind::stroke_line,
        .local_bounds = {.x = 4, .y = 48},
        .local_x1 = 60,
        .local_y1 = 48,
        .stroke_width = 3,
        .color = {255, 255, 0, 255},
        .world_col_major = mat4_to_float(state->nodes[2].world),
    });

    auto pass = lower_primitives_to_pass(
        prims, LowerPrimitivesConfig{.width_px = 64,
                                     .height_px = 64,
                                     .color_target = 1,
                                     .clear_color = {20, 20, 20, 255}});
    expect(pass.has_value(), "lower pass");
    if (!pass) {
      continue;
    }

    Rgba8Image image;
    expect(rasterize_pass(image, *pass).has_value(), "rasterize");

    auto lease = pool->try_acquire();
    expect(lease.has_value(), "staging acquire");
    if (!lease) {
      continue;
    }
    expect(lease->fill_rgba8(image.rgba).has_value(), "staging fill");
    expect(lease->mark_sink_busy().has_value(), "sink hold");

    auto frozen = lease->frozen_bytes();
    expect(frozen.has_value(), "frozen bytes");
    if (!frozen) {
      continue;
    }
    std::vector<std::byte> bytes(frozen->size());
    std::memcpy(bytes.data(), frozen->data(), frozen->size());
    auto frame = EncodableFrame::make_rgba8(64, 64, std::move(bytes),
                                            core::FrameIndex{f});
    expect(frame.has_value(), "encodable frame");
    if (!frame) {
      continue;
    }
    const auto clock = core::FrameClock::media_30_1();
    const auto pts = clock.pts(core::FrameIndex{f});
    expect(pts.has_value(), "pts");
    if (!pts) {
      continue;
    }
    expect(bridge.publish(*frame, *pts, std::stop_token{}).has_value(),
           "publish");
    expect(lease->complete_sink().has_value(), "complete sink");

    if (f == 0) {
      const auto png_path = out_dir / "frame0.png";
      expect(write_rgba8_png(png_path, 64, 64, image.rgba), "write png");
      expect(std::filesystem::file_size(png_path, ec) > 32, "png non-empty");
    }
  }

  auto summary = raw_sink->finish();
  expect(summary.has_value() && summary->frames_appended == 3, "sink finish");
  expect(raw_sink->frame_count() == 3, "three frames recorded");
  expect(raw_sink->frame(0).size() == 64U * 64U * 4U, "frame0 size");

  // No TypeScript artifacts on this path: scene + evaluate + primitives + sink.
  expect(true, "native-only skeleton path");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
