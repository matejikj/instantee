#include <instantee/export_core/rec709_handoff.hpp>

#include <string_view>
#include <utility>

namespace instantee::export_core {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "rec709_handoff", message};
}

[[nodiscard]] std::expected<std::vector<std::byte>, diagnostics::Error>
flatten_to_opaque_bytes(std::span<const std::uint8_t> straight_rgba) {
  if (straight_rgba.size() % 4U != 0) {
    return std::unexpected(make_invalid(
        "rgba_size_mismatch", "RGBA8 buffer size must be a multiple of 4"));
  }
  std::vector<std::byte> out(straight_rgba.size());
  for (std::size_t i = 0; i < straight_rgba.size(); i += 4) {
    const std::uint16_t a = straight_rgba[i + 3];
    out[i] = static_cast<std::byte>(
        (static_cast<std::uint16_t>(straight_rgba[i]) * a + 127U) / 255U);
    out[i + 1] = static_cast<std::byte>(
        (static_cast<std::uint16_t>(straight_rgba[i + 1]) * a + 127U) / 255U);
    out[i + 2] = static_cast<std::byte>(
        (static_cast<std::uint16_t>(straight_rgba[i + 2]) * a + 127U) / 255U);
    out[i + 3] = static_cast<std::byte>(255);
  }
  return out;
}

} // namespace

std::expected<EncodableFrame, diagnostics::Error>
make_rec709_encodable_frame(std::uint32_t width_px, std::uint32_t height_px,
                            std::span<const std::uint8_t> straight_rec709_rgba8,
                            core::FrameIndex frame_index) {
  if (width_px == 0 || height_px == 0) {
    return std::unexpected(make_invalid(
        "invalid_frame_extent", "Rec.709 frame width/height must be > 0"));
  }
  const std::size_t expected = static_cast<std::size_t>(width_px) *
                               static_cast<std::size_t>(height_px) * 4U;
  if (straight_rec709_rgba8.size() != expected) {
    return std::unexpected(make_invalid(
        "rgba_size_mismatch",
        "Rec.709 RGBA8 buffer size does not match width*height*4"));
  }
  auto opaque = flatten_to_opaque_bytes(straight_rec709_rgba8);
  if (!opaque) {
    return std::unexpected(opaque.error());
  }
  return EncodableFrame::make_rgba8(width_px, height_px, std::move(*opaque),
                                    frame_index);
}

std::expected<void, diagnostics::Error>
publish_rec709_rgba8(FrameBridge &bridge, std::uint32_t width_px,
                     std::uint32_t height_px,
                     std::span<const std::uint8_t> straight_rec709_rgba8,
                     core::FrameIndex frame_index, core::RationalTime pts,
                     std::stop_token stop) {
  auto frame = make_rec709_encodable_frame(width_px, height_px,
                                           straight_rec709_rgba8, frame_index);
  if (!frame) {
    return std::unexpected(frame.error());
  }
  return bridge.publish(*frame, pts, stop);
}

} // namespace instantee::export_core
