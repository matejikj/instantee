#pragma once

#include <compression.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace instantee::spike::test {

// Minimal RGBA8 PNG writer for fixture verification. Uses the system zlib
// compressor; not a general imaging dependency for the engine.
[[nodiscard]] inline bool write_rgba8_png(const std::string &path,
                                          std::uint32_t width,
                                          std::uint32_t height,
                                          std::span<const std::uint8_t> rgba) {
  const std::size_t expected =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
  if (rgba.size() != expected || width == 0 || height == 0) {
    return false;
  }

  std::vector<std::uint8_t> raw;
  raw.resize((static_cast<std::size_t>(width) * 4U + 1U) * height);
  for (std::uint32_t y = 0; y < height; ++y) {
    const std::size_t dst = static_cast<std::size_t>(y) *
                            (static_cast<std::size_t>(width) * 4U + 1U);
    raw[dst] = 0; // filter None
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

  std::FILE *file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    return false;
  }
  constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71,
                                                  13,  10, 26, 10};
  bool ok = std::fwrite(signature.data(), 1, signature.size(), file) ==
            signature.size();
  std::array<std::uint8_t, 13> ihdr{};
  ihdr[0] = static_cast<std::uint8_t>((width >> 24) & 0xFFU);
  ihdr[1] = static_cast<std::uint8_t>((width >> 16) & 0xFFU);
  ihdr[2] = static_cast<std::uint8_t>((width >> 8) & 0xFFU);
  ihdr[3] = static_cast<std::uint8_t>(width & 0xFFU);
  ihdr[4] = static_cast<std::uint8_t>((height >> 24) & 0xFFU);
  ihdr[5] = static_cast<std::uint8_t>((height >> 16) & 0xFFU);
  ihdr[6] = static_cast<std::uint8_t>((height >> 8) & 0xFFU);
  ihdr[7] = static_cast<std::uint8_t>(height & 0xFFU);
  ihdr[8] = 8;  // bit depth
  ihdr[9] = 6;  // RGBA
  ihdr[10] = 0; // compression
  ihdr[11] = 0; // filter
  ihdr[12] = 0; // interlace
  ok = ok && write_chunk(file, "IHDR", ihdr);
  ok = ok && write_chunk(file, "IDAT", compressed);
  ok = ok && write_chunk(file, "IEND", {});
  ok = std::fclose(file) == 0 && ok;
  return ok;
}

} // namespace instantee::spike::test
