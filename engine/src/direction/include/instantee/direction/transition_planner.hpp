#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// CDS semantic transition kinds. Concrete Scene IR blends are a smaller set
// (cut / dissolve / wipe); the planner performs that lowering.
enum class TransitionKind : std::uint8_t {
  hard_cut = 1,
  match_cut,
  wipe,
  reveal,
};

struct ShotTiming {
  std::string id;
  std::uint64_t start_frame = 0;
  std::uint64_t duration_frames = 0;
};

struct TransitionIntent {
  std::string id;
  std::string from_shot_id;
  std::string to_shot_id;
  TransitionKind kind = TransitionKind::hard_cut;
  // Required > 0 for wipe/reveal. Must be 0 (or nullopt) for cuts.
  std::optional<std::uint64_t> overlap_frames{};
};

struct PlannedTransition {
  std::string id;
  std::string from_shot_id;
  std::string to_shot_id;
  std::uint32_t from_shot_index = 0;
  std::uint32_t to_shot_index = 0;
  std::uint64_t start_frame = 0;
  std::uint64_t duration_frames = 0;
  // Scene IR blend id: "cut" | "dissolve" | "wipe".
  std::string blend = "cut";
  TransitionKind kind = TransitionKind::hard_cut;
  // Stable layer biases for overlap compositing: incoming paints above.
  std::int32_t from_layer_order = 0;
  std::int32_t to_layer_order = 1;
};

struct TransitionPlan {
  std::uint64_t film_duration_frames = 0;
  std::vector<PlannedTransition> spans;
};

// Plan explicit transition spans over a packed shot timeline. Film duration
// equals the packed shot coverage — spans never append or double-count frames.
[[nodiscard]] std::expected<TransitionPlan, diagnostics::Error>
plan_transitions(std::span<const ShotTiming> shots,
                 std::span<const TransitionIntent> intents);

[[nodiscard]] constexpr std::string_view
transition_kind_id(TransitionKind kind) noexcept {
  switch (kind) {
  case TransitionKind::hard_cut:
    return "hard_cut";
  case TransitionKind::match_cut:
    return "match_cut";
  case TransitionKind::wipe:
    return "wipe";
  case TransitionKind::reveal:
    return "reveal";
  }
  return "unknown";
}

[[nodiscard]] std::expected<TransitionKind, diagnostics::Error>
parse_transition_kind(std::string_view text);

// Map authoring blend tokens onto CDS kinds (cut→hard_cut, dissolve→reveal).
[[nodiscard]] std::expected<TransitionKind, diagnostics::Error>
transition_kind_from_authoring_blend(std::string_view blend);

} // namespace instantee::direction
