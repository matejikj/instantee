#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/directed_film.hpp>
#include <instantee/direction/style_bible.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Editorial headline motion modes. Compiled once; frame sampling is pure C++
// (no CDS / TypeScript / scripting in the frame loop).
enum class HeadlineRevealMode : std::uint8_t {
  mask_reveal = 1, // sequential wipe mask across kinetic units
  word_stagger,    // per-word opacity + entrance offset
  slam,            // short scale/opacity slam (typically one block unit)
};

struct HeadlineRevealParams {
  HeadlineRevealMode mode = HeadlineRevealMode::word_stagger;
  MotionEase ease = MotionEase::ease_out;
  std::uint32_t duration_frames = 1;
  // Peak opacity / slam overshoot strength in (0, 1].
  double intensity = 0.8;
  std::uint32_t stagger_frames = 0;
};

struct HeadlineUnitFrame {
  std::uint32_t unit_index = 0;
  double progress = 0.0;
  double opacity = 0.0;
  double mask = 0.0;
  double scale = 1.0;
  // Entrance slide in 26.6 px; zero once settled.
  std::int32_t offset_y_26_6 = 0;
};

// Immutable timing table for one headline gesture over `unit_count` kinetic
// units (words for stagger/mask, usually 1 for slam).
struct HeadlineRevealProgram {
  HeadlineRevealParams params{};
  std::uint32_t unit_count = 0;
  std::uint32_t total_frames = 0;
  std::vector<std::uint32_t> unit_start_frame{};
  std::vector<std::uint32_t> unit_duration_frames{};
};

[[nodiscard]] constexpr std::string_view
headline_reveal_mode_id(HeadlineRevealMode mode) noexcept {
  switch (mode) {
  case HeadlineRevealMode::mask_reveal:
    return "mask_reveal";
  case HeadlineRevealMode::word_stagger:
    return "word_stagger";
  case HeadlineRevealMode::slam:
    return "slam";
  }
  return "unknown";
}

// Map a Style Bible motion token id to params. Known ids: headline_reveal,
// mask_reveal, accent_slam. intensity/duration/stagger/ease come from `token`.
[[nodiscard]] std::expected<HeadlineRevealParams, diagnostics::Error>
make_headline_reveal_params(std::string_view preset_id,
                            const MotionToken &token);

// Optional intensity/duration overrides after resolving a preset (still no JS).
[[nodiscard]] std::expected<HeadlineRevealParams, diagnostics::Error>
scale_headline_reveal_params(HeadlineRevealParams params, double intensity,
                             std::uint32_t duration_frames);

[[nodiscard]] std::expected<HeadlineRevealProgram, diagnostics::Error>
compile_headline_reveal(const HeadlineRevealParams &params,
                        std::uint32_t unit_count);

// Sample all units at a shot-local frame. Allocates only the returned vector.
[[nodiscard]] std::expected<std::vector<HeadlineUnitFrame>, diagnostics::Error>
sample_headline_reveal(const HeadlineRevealProgram &program,
                       std::uint32_t frame);

// Lower to concrete DirectedTracks (opacity / mask / scale) for CDS publish.
[[nodiscard]] std::expected<std::vector<DirectedTrack>, diagnostics::Error>
lower_headline_reveal_tracks(const HeadlineRevealProgram &program,
                             std::string_view shot_id,
                             std::span<const std::string> unit_target_ids);

} // namespace instantee::direction
