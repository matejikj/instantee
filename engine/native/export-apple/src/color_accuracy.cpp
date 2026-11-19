#include <instantee/export_apple/color_path.hpp>

#include <algorithm>
#include <format>
#include <string>

namespace instantee::spike::apple {
namespace {

constexpr const char *kPhase = "color_accuracy";

[[nodiscard]] std::uint8_t channel_delta(std::uint8_t a,
                                         std::uint8_t b) noexcept {
  return a > b ? static_cast<std::uint8_t>(a - b)
               : static_cast<std::uint8_t>(b - a);
}

} // namespace

std::expected<ColorAccuracyStats, Error> compare_preview_rgba8_to_decoded_bgra8(
    std::span<const std::uint8_t> preview_rgba, std::uint32_t width,
    std::uint32_t height, const DecodedBgraFrame &decoded,
    std::span<const ColorAccuracySample> samples,
    std::uint8_t max_channel_delta) {
  if (width == 0 || height == 0) {
    return std::unexpected(Error{ExitCode::invalid_input,
                                 "color_accuracy.extent", kPhase,
                                 "Preview extent must be non-zero"});
  }
  const std::size_t expected_bytes =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
  if (preview_rgba.size() != expected_bytes) {
    return std::unexpected(
        Error{ExitCode::invalid_input, "color_accuracy.rgba_size", kPhase,
              "Preview RGBA size must equal width*height*4"});
  }
  if (decoded.format.width != width || decoded.format.height != height) {
    return std::unexpected(
        Error{ExitCode::export_failure, "color_accuracy.size_mismatch", kPhase,
              std::format("Decoded frame {}x{} does not match preview {}x{}",
                          decoded.format.width, decoded.format.height, width,
                          height)});
  }
  if (decoded.bgra.size() != expected_bytes) {
    return std::unexpected(
        Error{ExitCode::export_failure, "color_accuracy.bgra_size", kPhase,
              "Decoded BGRA size must equal width*height*4"});
  }
  if (samples.empty()) {
    return std::unexpected(Error{ExitCode::invalid_input,
                                 "color_accuracy.no_samples", kPhase,
                                 "At least one sample point is required"});
  }

  ColorAccuracyStats stats{};
  for (const ColorAccuracySample &sample : samples) {
    if (sample.x >= width || sample.y >= height) {
      return std::unexpected(
          Error{ExitCode::invalid_input, "color_accuracy.sample_oob", kPhase,
                std::format("Sample ({}, {}) is outside {}x{}", sample.x,
                            sample.y, width, height)});
    }
    const std::size_t index =
        (static_cast<std::size_t>(sample.y) * width + sample.x) * 4U;
    const Rgba8Color rgba{preview_rgba[index], preview_rgba[index + 1U],
                          preview_rgba[index + 2U], preview_rgba[index + 3U]};
    const Bgra8Color expected = flatten_straight_rgba8_over_black(rgba);
    const Bgra8Color actual{decoded.bgra[index], decoded.bgra[index + 1U],
                            decoded.bgra[index + 2U], decoded.bgra[index + 3U]};
    const std::uint8_t delta = std::max({channel_delta(actual.b, expected.b),
                                         channel_delta(actual.g, expected.g),
                                         channel_delta(actual.r, expected.r),
                                         channel_delta(actual.a, expected.a)});
    stats.max_channel_delta = std::max(stats.max_channel_delta, delta);
    ++stats.samples_checked;
    if (delta > max_channel_delta) {
      ++stats.failing_samples;
    }
  }

  if (stats.failing_samples != 0U) {
    return std::unexpected(
        Error{ExitCode::export_failure, "color_accuracy.tolerance", kPhase,
              std::format(
                  "{} of {} samples exceed channel delta {} (observed max {})",
                  stats.failing_samples, stats.samples_checked,
                  max_channel_delta, stats.max_channel_delta)});
  }
  return stats;
}

} // namespace instantee::spike::apple
