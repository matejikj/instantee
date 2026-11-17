#include <instantee/preview/frame_png.hpp>

#include <compression.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::preview {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "preview_png", message};
}

[[nodiscard]] diagnostics::Error make_io(std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::report_failure,
                            diagnostics::ErrorKind::report_failure,
                            "png_write_failed", "preview_png", message};
}

[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> data) noexcept {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::uint8_t byte : data) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = -(crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

[[nodiscard]] bool write_u32(std::FILE *file, std::uint32_t value) noexcept {
  const std::array<std::uint8_t, 4> bytes{
      static_cast<std::uint8_t>((value >> 24) & 0xFFU),
      static_cast<std::uint8_t>((value >> 16) & 0xFFU),
      static_cast<std::uint8_t>((value >> 8) & 0xFFU),
      static_cast<std::uint8_t>(value & 0xFFU)};
  return std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
}

[[nodiscard]] bool write_chunk(std::FILE *file, const char type[4],
                               std::span<const std::uint8_t> payload) {
  if (!write_u32(file, static_cast<std::uint32_t>(payload.size()))) {
    return false;
  }
  if (std::fwrite(type, 1, 4, file) != 4) {
    return false;
  }
  if (!payload.empty() &&
      std::fwrite(payload.data(), 1, payload.size(), file) != payload.size()) {
    return false;
  }
  std::vector<std::uint8_t> for_crc(4 + payload.size());
  std::memcpy(for_crc.data(), type, 4);
  if (!payload.empty()) {
    std::memcpy(for_crc.data() + 4, payload.data(), payload.size());
  }
  return write_u32(file, crc32(for_crc));
}

} // namespace

std::expected<void, diagnostics::Error>
write_rgba8_png(const std::filesystem::path &path, std::uint32_t width_px,
                std::uint32_t height_px, std::span<const std::uint8_t> rgba,
                PreviewColorSpace color_space) {
  if (width_px == 0 || height_px == 0) {
    return std::unexpected(
        make_invalid("invalid_png_extent", "PNG width/height must be > 0"));
  }
  const std::size_t expected = static_cast<std::size_t>(width_px) *
                               static_cast<std::size_t>(height_px) * 4U;
  if (rgba.size() != expected) {
    return std::unexpected(make_invalid(
        "rgba_size_mismatch", "RGBA8 buffer size must equal width*height*4"));
  }
  if (color_space != PreviewColorSpace::rec709_sdr) {
    return std::unexpected(
        make_invalid("unsupported_preview_color_space",
                     "only rec709-sdr preview PNG encoding is supported"));
  }

  std::vector<std::uint8_t> raw;
  raw.resize((static_cast<std::size_t>(width_px) * 4U + 1U) * height_px);
  for (std::uint32_t y = 0; y < height_px; ++y) {
    const std::size_t dst = static_cast<std::size_t>(y) *
                            (static_cast<std::size_t>(width_px) * 4U + 1U);
    raw[dst] = 0;
    std::memcpy(raw.data() + dst + 1U,
                rgba.data() + static_cast<std::size_t>(y) * width_px * 4U,
                static_cast<std::size_t>(width_px) * 4U);
  }

  std::vector<std::uint8_t> compressed(raw.size() + 128U);
  const std::size_t encoded = compression_encode_buffer(
      compressed.data(), compressed.size(), raw.data(), raw.size(), nullptr,
      COMPRESSION_ZLIB);
  if (encoded == 0) {
    return std::unexpected(make_io("zlib compression failed for PNG IDAT"));
  }
  compressed.resize(encoded);

  const std::string path_str = path.string();
  std::FILE *file = std::fopen(path_str.c_str(), "wb");
  if (file == nullptr) {
    return std::unexpected(make_io("cannot open PNG output path"));
  }

  constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71,
                                                  13,  10, 26, 10};
  bool ok = std::fwrite(signature.data(), 1, signature.size(), file) ==
            signature.size();

  std::array<std::uint8_t, 13> ihdr{};
  ihdr[0] = static_cast<std::uint8_t>((width_px >> 24) & 0xFFU);
  ihdr[1] = static_cast<std::uint8_t>((width_px >> 16) & 0xFFU);
  ihdr[2] = static_cast<std::uint8_t>((width_px >> 8) & 0xFFU);
  ihdr[3] = static_cast<std::uint8_t>(width_px & 0xFFU);
  ihdr[4] = static_cast<std::uint8_t>((height_px >> 24) & 0xFFU);
  ihdr[5] = static_cast<std::uint8_t>((height_px >> 16) & 0xFFU);
  ihdr[6] = static_cast<std::uint8_t>((height_px >> 8) & 0xFFU);
  ihdr[7] = static_cast<std::uint8_t>(height_px & 0xFFU);
  ihdr[8] = 8;
  ihdr[9] = 6;
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;
  ok = ok && write_chunk(file, "IHDR", ihdr);

  // sRGB rendering intent = perceptual (0). Documents SDR transfer intent.
  const std::array<std::uint8_t, 1> srgb{0};
  ok = ok && write_chunk(file, "sRGB", srgb);
  ok = ok && write_chunk(file, "IDAT", compressed);
  ok = ok && write_chunk(file, "IEND", {});
  const int closed = std::fclose(file);
  if (!ok || closed != 0) {
    return std::unexpected(make_io("failed while writing PNG file"));
  }
  return {};
}

} // namespace instantee::preview
