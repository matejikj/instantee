#pragma once

#include <instantee/core_model/ids.hpp>
#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Semantic direction intents consumed by CDS before compile. These remain
// unresolved authoring values until a later DirectedFilm lowering step.
enum class IntentKind : std::uint8_t {
  reveal = 1,
  focus,
  compare,
  explain,
  build_suspense,
};

struct SemanticIntent {
  IntentKind kind = IntentKind::reveal;
  std::vector<core::NodeId> targets{};
  double intensity = 0.0;
  double importance = 0.0;
  std::optional<std::uint32_t> duration_frames{};
};

[[nodiscard]] constexpr std::string_view
intent_kind_id(IntentKind kind) noexcept {
  switch (kind) {
  case IntentKind::reveal:
    return "reveal";
  case IntentKind::focus:
    return "focus";
  case IntentKind::compare:
    return "compare";
  case IntentKind::explain:
    return "explain";
  case IntentKind::build_suspense:
    return "build_suspense";
  }
  return "unknown";
}

[[nodiscard]] std::expected<IntentKind, diagnostics::Error>
parse_intent_kind(std::string_view text);

// Builds a typed intent and rejects invalid parameters or target ids that are
// absent from `known_node_ids` (shot-local node namespace).
[[nodiscard]] std::expected<SemanticIntent, diagnostics::Error>
make_semantic_intent(IntentKind kind,
                     std::span<const std::string_view> target_ids,
                     double intensity, double importance,
                     std::optional<std::uint32_t> duration_frames,
                     std::span<const std::string_view> known_node_ids);

[[nodiscard]] std::expected<SemanticIntent, diagnostics::Error>
make_semantic_intent(std::string_view kind_text,
                     std::span<const std::string_view> target_ids,
                     double intensity, double importance,
                     std::optional<std::uint32_t> duration_frames,
                     std::span<const std::string_view> known_node_ids);

} // namespace instantee::direction
