#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace instantee::core {

// Largest integer that every IEEE-754 binary64 / JS Number represents exactly.
inline constexpr std::uint64_t kJsSafeIntegerMax =
    (std::uint64_t{1} << 53) - 1U;

// How a designated uint64 appears in JSON before a full document parser lands.
enum class Uint64JsonForm : std::uint8_t { number, decimal_string };

// Encode/decode policy for seed and other designated uint64 fields. Number form
// is limited to kJsSafeIntegerMax; the full uint64 range uses a decimal string
// without leading zeros (except "0"). Overflow and malformed spellings fail.
class Uint64Encoding {
public:
  [[nodiscard]] static std::expected<std::uint64_t, diagnostics::Error>
  parse_decimal(std::string_view text) noexcept;

  [[nodiscard]] static std::string format_decimal(std::uint64_t value);

  // Rejects number-form values above the JS safe integer max so TypeScript
  // cannot silently lose low bits when reading a JSON number.
  [[nodiscard]] static std::expected<std::uint64_t, diagnostics::Error>
  from_json_number(std::uint64_t value) noexcept;

  [[nodiscard]] static std::expected<std::uint64_t, diagnostics::Error>
  from_json_string(std::string_view text) noexcept;

  [[nodiscard]] static Uint64JsonForm
  preferred_form(std::uint64_t value) noexcept;
};

// Documented omitted-field defaults for authoring / Scene IR v1. Kept in sync
// with contracts/defaults/v1.defaults.json by the contract test suite.
struct ContractDefaultsV1 {
  static constexpr bool node_visible = true;
  static constexpr double transform_opacity = 1.0;
  static constexpr double camera_near = 0.1;
  static constexpr double camera_far = 1000.0;
  static constexpr std::string_view camera_projection = "orthographic";
  static constexpr std::string_view canvas_color = "rec709-sdr";
  static constexpr std::string_view asset_color_profile = "untagged";
  static constexpr double material_opacity = 1.0;
  static constexpr bool geometry_closed = true;
  static constexpr std::uint32_t transition_overlap_frames = 0;
  static constexpr std::string_view transition_blend = "cut";
};

} // namespace instantee::core
