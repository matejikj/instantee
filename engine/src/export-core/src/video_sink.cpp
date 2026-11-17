#include <instantee/export_core/video_sink.hpp>

#include <string_view>

namespace instantee::export_core {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "export_core", message};
}

} // namespace

std::expected<EncodableFrame, diagnostics::Error>
EncodableFrame::make_rgba8(std::uint32_t width, std::uint32_t height,
                           std::vector<std::byte> rgba8,
                           core::FrameIndex frame_index) {
  if (width == 0 || height == 0) {
    return std::unexpected(
        make_invalid("invalid_frame_extent", "width and height must be > 0"));
  }
  const std::uint64_t pixels =
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
  const std::uint64_t expected = pixels * 4ULL;
  if (expected > static_cast<std::uint64_t>(rgba8.max_size()) ||
      rgba8.size() != static_cast<std::size_t>(expected)) {
    return std::unexpected(make_invalid(
        "invalid_frame_bytes", "rgba8 size must equal width*height*4"));
  }
  return EncodableFrame{width, height, std::move(rgba8), frame_index};
}

EncodableFrame::EncodableFrame(std::uint32_t width, std::uint32_t height,
                               std::vector<std::byte> rgba8,
                               core::FrameIndex frame_index) noexcept
    : width_(width), height_(height), frame_index_(frame_index),
      rgba8_(std::move(rgba8)) {}

EncodableFrame::EncodableFrame(EncodableFrame &&other) noexcept
    : width_(other.width_), height_(other.height_),
      frame_index_(other.frame_index_), rgba8_(std::move(other.rgba8_)) {
  other.width_ = 0;
  other.height_ = 0;
  other.frame_index_ = core::FrameIndex{};
}

EncodableFrame &EncodableFrame::operator=(EncodableFrame &&other) noexcept {
  if (this != &other) {
    width_ = other.width_;
    height_ = other.height_;
    frame_index_ = other.frame_index_;
    rgba8_ = std::move(other.rgba8_);
    other.width_ = 0;
    other.height_ = 0;
    other.frame_index_ = core::FrameIndex{};
  }
  return *this;
}

std::vector<std::byte> EncodableFrame::release_pixels() noexcept {
  width_ = 0;
  height_ = 0;
  frame_index_ = core::FrameIndex{};
  return std::move(rgba8_);
}

} // namespace instantee::export_core
