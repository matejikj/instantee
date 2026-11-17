#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/motion_grammar.hpp>
#include <instantee/direction/semantic_intent.hpp>
#include <instantee/direction/track_precedence.hpp>
#include <instantee/direction/transition_planner.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

enum class DirectionDecisionSeverity : std::uint8_t {
  info = 1,
  warning,
};

struct DirectionWarning {
  std::string code;
  std::string message;
  std::string shot_id;
  std::string object_id;
  DirectionDecisionSeverity severity = DirectionDecisionSeverity::warning;
};

// One intent → motion-grammar binding choice for a shot.
struct IntentDecision {
  std::string shot_id;
  IntentKind intent = IntentKind::reveal;
  GesturePattern pattern = GesturePattern::reveal;
  std::string package_id;
  TrackSource source = TrackSource::style_bible;
  double intensity = 0.0;
  double importance = 0.0;
  // Machine-stable reason, e.g. "motion_grammar_binding".
  std::string reason_code;
};

// Final winning animation channel after precedence resolution.
struct ChannelDecision {
  std::string shot_id;
  std::string object_id;
  std::string property;
  std::string track_id;
  TrackSource source = TrackSource::framework_default;
  std::string reason_code;
  std::vector<DirectedKeyframe> keyframes; // shot-relative frames
};

struct TransitionDecision {
  std::string id;
  std::string from_shot_id;
  std::string to_shot_id;
  TransitionKind kind = TransitionKind::hard_cut;
  std::string blend;
  std::uint64_t start_frame = 0;
  std::uint64_t duration_frames = 0;
  std::string reason_code;
};

// Structured answer for "why does object X look like this at frame N?".
struct DirectionExplanation {
  std::string object_id;
  std::uint64_t frame = 0;
  std::string shot_id;
  std::string property;
  std::string track_id;
  TrackSource source = TrackSource::framework_default;
  std::string reason_code;
  double value = 0.0;
  std::optional<IntentKind> intent{};
  std::optional<GesturePattern> pattern{};
  std::string summary;
};

// Auditable CDS log. Value-owned; no platform handles. Snapshot text is
// deterministic for identical decision content.
class DirectionReport {
public:
  void add_intent(IntentDecision decision);
  void add_channel(ChannelDecision decision);
  void add_transition(TransitionDecision decision);
  void add_warning(DirectionWarning warning);

  [[nodiscard]] std::span<const IntentDecision> intents() const noexcept {
    return intents_;
  }
  [[nodiscard]] std::span<const ChannelDecision> channels() const noexcept {
    return channels_;
  }
  [[nodiscard]] std::span<const TransitionDecision>
  transitions() const noexcept {
    return transitions_;
  }
  [[nodiscard]] std::span<const DirectionWarning> warnings() const noexcept {
    return warnings_;
  }

  // Explain the final directed choice for an object at a shot-relative frame.
  // Prefers an opacity channel when multiple properties match the object.
  [[nodiscard]] std::expected<DirectionExplanation, diagnostics::Error>
  explain(std::string_view object_id, std::uint64_t frame) const;

  // Compact deterministic audit snapshot suitable for golden tests / logs.
  [[nodiscard]] std::string to_snapshot() const;

private:
  std::vector<IntentDecision> intents_;
  std::vector<ChannelDecision> channels_;
  std::vector<TransitionDecision> transitions_;
  std::vector<DirectionWarning> warnings_;
};

// Record a grammar-backed intent decision (pattern + overrides + source).
[[nodiscard]] IntentDecision
make_intent_decision(std::string_view shot_id, const SemanticIntent &intent,
                     const MotionGrammarBinding &binding, TrackSource source);

// Record winning channels from resolved tracks paired with their claim source.
[[nodiscard]] std::vector<ChannelDecision>
make_channel_decisions(std::string_view shot_id,
                       std::span<const TrackClaim> winning_claims);

[[nodiscard]] TransitionDecision
make_transition_decision(const PlannedTransition &planned);

[[nodiscard]] constexpr std::string_view
direction_decision_severity_id(DirectionDecisionSeverity severity) noexcept {
  switch (severity) {
  case DirectionDecisionSeverity::info:
    return "info";
  case DirectionDecisionSeverity::warning:
    return "warning";
  }
  return "unknown";
}

} // namespace instantee::direction
