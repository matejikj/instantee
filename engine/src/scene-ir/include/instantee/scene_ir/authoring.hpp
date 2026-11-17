#pragma once

#include <instantee/core_model/time.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/scene_ir/limits.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::scene_ir {

struct AuthoringIntent {
  std::string type;
  double intensity = 0;
  std::optional<double> importance;
  std::vector<std::string> target_ids;
};

struct AuthoringNode {
  std::string id;
  std::string type; // text, group, ...
  std::optional<std::string> text;
  std::optional<std::string> role;
  std::optional<double> translate_x;
  std::optional<double> translate_y;
  std::optional<double> opacity;
  bool visible = true;
  std::vector<AuthoringNode> children;
};

struct AuthoringShot {
  std::string id;
  std::uint64_t duration_frames = 0;
  std::optional<AuthoringIntent> intent;
  std::vector<AuthoringNode> nodes;
};

struct AuthoringDocument {
  std::string schema_version;
  std::string film_id;
  std::uint32_t canvas_width = 0;
  std::uint32_t canvas_height = 0;
  std::string canvas_color = "rec709-sdr";
  core::FrameRate frame_rate = core::FrameRate::media_30_1();
  std::uint64_t seed = 0;
  std::string style;
  std::vector<AuthoringShot> shots;
};

struct LoadOptions;

[[nodiscard]] std::expected<AuthoringDocument, diagnostics::Error>
load_authoring_json(std::span<const std::byte> bytes,
                    const LoadOptions &options);

[[nodiscard]] std::expected<AuthoringDocument, diagnostics::Error>
load_authoring_json(std::string_view text, const LoadOptions &options);

} // namespace instantee::scene_ir
