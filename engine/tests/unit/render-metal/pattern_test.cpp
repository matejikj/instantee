#include "png_rgba8.hpp"

#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/frame.hpp>
#include <instantee/render_metal/test_pattern.hpp>

#include <CommonCrypto/CommonDigest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using instantee::render_metal::Device;
using instantee::render_metal::expected_test_pattern_pixel;
using instantee::render_metal::frame_pixel;
using instantee::render_metal::kTestPatternHeight;
using instantee::render_metal::kTestPatternWidth;
using instantee::render_metal::Rgba8Frame;
using instantee::render_metal::TestPatternRenderer;

[[nodiscard]] std::string hex_sha256(std::span<const std::uint8_t> bytes) {
  std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
  CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(digest.size() * 2U, '0');
  for (std::size_t i = 0; i < digest.size(); ++i) {
    out[i * 2U] = kHex[(digest[i] >> 4) & 0xF];
    out[i * 2U + 1U] = kHex[digest[i] & 0xF];
  }
  return out;
}

[[nodiscard]] bool frames_equal(const Rgba8Frame &lhs, const Rgba8Frame &rhs) {
  return lhs.extent.width == rhs.extent.width &&
         lhs.extent.height == rhs.extent.height && lhs.rgba == rhs.rgba;
}

[[nodiscard]] int verify_frame(const Rgba8Frame &frame, std::uint32_t index) {
  if (!frame.valid_dimensions()) {
    return 10 + static_cast<int>(index);
  }
  for (std::uint32_t y = 0; y < kTestPatternHeight; ++y) {
    for (std::uint32_t x = 0; x < kTestPatternWidth; ++x) {
      if (frame_pixel(frame, x, y) !=
          expected_test_pattern_pixel(index, x, y)) {
        return 20 + static_cast<int>(index);
      }
    }
  }
  return 0;
}

} // namespace

int main() {
  auto device = Device::open();
  if (!device) {
    return 1;
  }
  auto renderer = TestPatternRenderer::create(*device);
  if (!renderer) {
    return 2;
  }

  auto frame0 = renderer->render(0);
  auto frame1 = renderer->render(1);
  if (!frame0 || !frame1) {
    return 3;
  }
  if (const int code = verify_frame(*frame0, 0); code != 0) {
    return code;
  }
  if (const int code = verify_frame(*frame1, 1); code != 0) {
    return code;
  }
  if (frames_equal(*frame0, *frame1)) {
    return 4;
  }
  if (renderer->render(2)) {
    return 5;
  }

  const auto hash0 = hex_sha256(frame0->rgba);
  const auto hash1 = hex_sha256(frame1->rgba);
  constexpr auto kFrame0Rgba =
      "af65ab7e396a16b1a358d820e920658b4455d361458d26bd41bb0af172cee362";
  constexpr auto kFrame1Rgba =
      "ce7fc16f3e129912549013064d5a7eec58ec2adeedd7b8c1f0d55cbea6bcc451";
  if (hash0 != kFrame0Rgba || hash1 != kFrame1Rgba) {
    return 6;
  }

  const auto directory =
      std::filesystem::temp_directory_path() / "instantee-test-pattern-XXXXXX";
  std::string template_path = directory.string();
  if (mkdtemp(template_path.data()) == nullptr) {
    return 7;
  }
  const std::filesystem::path out_dir{template_path};
  const auto png0 = (out_dir / "frame0.png").string();
  const auto png1 = (out_dir / "frame1.png").string();
  if (!instantee::spike::test::write_rgba8_png(
          png0, kTestPatternWidth, kTestPatternHeight, frame0->rgba) ||
      !instantee::spike::test::write_rgba8_png(
          png1, kTestPatternWidth, kTestPatternHeight, frame1->rgba)) {
    return 8;
  }

  auto read_file = [](const std::string &path) {
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
      return std::vector<std::uint8_t>{};
    }
    std::vector<std::uint8_t> bytes;
    std::array<std::uint8_t, 4096> buffer{};
    while (!std::feof(file) && std::ferror(file) == 0) {
      const auto read = std::fread(buffer.data(), 1, buffer.size(), file);
      if (read == 0) {
        break;
      }
      bytes.insert(bytes.end(), buffer.begin(),
                   buffer.begin() + static_cast<std::ptrdiff_t>(read));
    }
    const bool ok = std::ferror(file) == 0;
    std::fclose(file);
    if (!ok) {
      return std::vector<std::uint8_t>{};
    }
    return bytes;
  };

  const auto png0_bytes = read_file(png0);
  const auto png1_bytes = read_file(png1);
  if (png0_bytes.size() < 32 || png1_bytes.size() < 32) {
    return 9;
  }
  if (png0_bytes[0] != 137 || png0_bytes[1] != 80 || png0_bytes[2] != 78 ||
      png0_bytes[3] != 71) {
    return 11;
  }
  const auto png_hash0 = hex_sha256(png0_bytes);
  const auto png_hash1 = hex_sha256(png1_bytes);
  constexpr auto kFrame0Png =
      "beee6860e51fb89d5796cf0ac0c41cb71b64d49b2aef0a69b05b42bc00b2b80b";
  constexpr auto kFrame1Png =
      "903adf3f95b4b07eea835a6a628984fb767c37c8053673cdaad22475ff96dabd";
  if (png_hash0 != kFrame0Png || png_hash1 != kFrame1Png) {
    return 12;
  }

  // Persist checksums for the evidence allowlist when INSTANTEE_PATTERN_OUT is
  // set.
  if (const char *out = std::getenv("INSTANTEE_PATTERN_OUT"); out != nullptr) {
    const std::filesystem::path evidence{out};
    std::filesystem::create_directories(evidence);
    std::filesystem::copy_file(
        png0, evidence / "frame0.png",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(
        png1, evidence / "frame1.png",
        std::filesystem::copy_options::overwrite_existing);
    std::FILE *report =
        std::fopen((evidence / "checksums.json").string().c_str(), "wb");
    if (report == nullptr) {
      return 13;
    }
    std::fprintf(report,
                 "{\n"
                 "  \"width\": %u,\n"
                 "  \"height\": %u,\n"
                 "  \"channels\": 4,\n"
                 "  \"frame0_rgba_sha256\": \"%s\",\n"
                 "  \"frame1_rgba_sha256\": \"%s\",\n"
                 "  \"frame0_png_sha256\": \"%s\",\n"
                 "  \"frame1_png_sha256\": \"%s\"\n"
                 "}\n",
                 kTestPatternWidth, kTestPatternHeight, hash0.c_str(),
                 hash1.c_str(), png_hash0.c_str(), png_hash1.c_str());
    std::fclose(report);
  }

  std::filesystem::remove_all(out_dir);
}
