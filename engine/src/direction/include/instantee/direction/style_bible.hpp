#pragma once

#include <instantee/core_model/color.hpp>
#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace instantee::direction {

// Style Bible schema_version currently accepted by the loader.
inline constexpr std::string_view kStyleBibleSchemaVersion = "1.0.0";

enum class FontRole : std::uint8_t {
  headline = 1,
  subhead,
  body,
  caption,
  mono,
};

enum class MotionEase : std::uint8_t {
  linear = 1,
  ease_in,
  ease_out,
  ease_in_out,
  spring,
};

enum class CameraFraming : std::uint8_t {
  static_framing = 1,
  push_in,
  pull_out,
  pan,
  orbit,
};

enum class SafeAreaMode : std::uint8_t {
  reels_overlay = 1,
  reels_clear,
  full_frame,
};

struct ColorToken {
  core::LinearRgba value{};
};

struct FontToken {
  std::string family;
  FontRole role = FontRole::body;
  double size_scale = 1.0;
};

struct MaterialToken {
  std::string shader_feature;
  double opacity = 1.0;
  std::unordered_map<std::string, double> parameters;
};

struct SpacingToken {
  double px = 0.0;
};

struct MotionToken {
  MotionEase ease = MotionEase::linear;
  std::uint32_t duration_frames = 1;
  double intensity = 0.0;
  std::uint32_t stagger_frames = 0;
};

struct CameraToken {
  CameraFraming framing = CameraFraming::static_framing;
  MotionEase ease = MotionEase::ease_in_out;
};

struct SafeAreaPolicy {
  SafeAreaMode mode = SafeAreaMode::full_frame;
  double top_px = 0;
  double bottom_px = 0;
  double left_px = 0;
  double right_px = 0;
};

// Immutable Style Bible document. Ownership is value-based; no GPU/native
// handles. Distinct packages differ only by tokens — core-model is unchanged.
class StyleBible {
public:
  StyleBible() = default;

  [[nodiscard]] std::string_view schema_version() const noexcept {
    return schema_version_;
  }
  [[nodiscard]] std::string_view package_id() const noexcept {
    return package_id_;
  }
  [[nodiscard]] std::string_view version() const noexcept { return version_; }
  [[nodiscard]] std::string_view minimum_engine_version() const noexcept {
    return minimum_engine_version_;
  }

  [[nodiscard]] const std::unordered_map<std::string, ColorToken> &
  colors() const noexcept {
    return colors_;
  }
  [[nodiscard]] const std::unordered_map<std::string, FontToken> &
  fonts() const noexcept {
    return fonts_;
  }
  [[nodiscard]] const std::unordered_map<std::string, MaterialToken> &
  materials() const noexcept {
    return materials_;
  }
  [[nodiscard]] const std::unordered_map<std::string, SpacingToken> &
  spacing() const noexcept {
    return spacing_;
  }
  [[nodiscard]] const std::unordered_map<std::string, MotionToken> &
  motion() const noexcept {
    return motion_;
  }
  [[nodiscard]] const std::unordered_map<std::string, CameraToken> &
  camera() const noexcept {
    return camera_;
  }
  [[nodiscard]] const SafeAreaPolicy &safe_area() const noexcept {
    return safe_area_;
  }

private:
  friend std::expected<StyleBible, diagnostics::Error>
  load_style_bible_json(std::string_view text);

  std::string schema_version_;
  std::string package_id_;
  std::string version_;
  std::string minimum_engine_version_;
  std::unordered_map<std::string, ColorToken> colors_;
  std::unordered_map<std::string, FontToken> fonts_;
  std::unordered_map<std::string, MaterialToken> materials_;
  std::unordered_map<std::string, SpacingToken> spacing_;
  std::unordered_map<std::string, MotionToken> motion_;
  std::unordered_map<std::string, CameraToken> camera_;
  SafeAreaPolicy safe_area_{};
};

[[nodiscard]] std::expected<StyleBible, diagnostics::Error>
load_style_bible_json(std::string_view text);

[[nodiscard]] std::expected<StyleBible, diagnostics::Error>
load_style_bible_file(const std::filesystem::path &path);

[[nodiscard]] constexpr std::string_view font_role_id(FontRole role) noexcept {
  switch (role) {
  case FontRole::headline:
    return "headline";
  case FontRole::subhead:
    return "subhead";
  case FontRole::body:
    return "body";
  case FontRole::caption:
    return "caption";
  case FontRole::mono:
    return "mono";
  }
  return "unknown";
}

} // namespace instantee::direction
