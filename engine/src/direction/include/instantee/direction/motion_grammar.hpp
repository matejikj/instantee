#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/semantic_intent.hpp>
#include <instantee/direction/style_bible.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace instantee::direction {

// Curated gesture patterns. Style packages bind intents to these operators;
// render-metal never sees style names.
enum class GesturePattern : std::uint8_t {
  reveal = 1,
  stagger,
  settle,
  camera_move,
};

struct MotionGrammarBinding {
  std::string package_id;
  IntentKind intent = IntentKind::reveal;
  GesturePattern pattern = GesturePattern::reveal;
  MotionEase ease = MotionEase::ease_out;
  // Fraction of the shot duration used by the gesture, clamped to (0, 1].
  double duration_scale = 0.5;
  // Multiplier applied to intent.intensity for peak amplitude.
  double intensity_scale = 1.0;
  std::uint32_t stagger_frames = 0;
  std::string property = "opacity";
  double from_value = 0.0;
  // Optional mid keyframe (settle / suspense). Empty when unused.
  bool has_hold = false;
  double hold_value = 0.0;
  double hold_fraction = 0.5;
};

class MotionGrammarRegistry {
public:
  [[nodiscard]] std::expected<void, diagnostics::Error>
  register_binding(MotionGrammarBinding binding);

  [[nodiscard]] bool contains(std::string_view package_id,
                              IntentKind intent) const;

  [[nodiscard]] std::expected<MotionGrammarBinding, diagnostics::Error>
  resolve(std::string_view package_id, IntentKind intent) const;

  [[nodiscard]] std::vector<std::string> package_ids() const;

private:
  static std::string key_for(std::string_view package_id, IntentKind intent);
  std::unordered_map<std::string, MotionGrammarBinding> by_key_;
};

[[nodiscard]] MotionGrammarRegistry make_builtin_motion_grammar_registry();

[[nodiscard]] constexpr std::string_view
gesture_pattern_id(GesturePattern pattern) noexcept {
  switch (pattern) {
  case GesturePattern::reveal:
    return "reveal";
  case GesturePattern::stagger:
    return "stagger";
  case GesturePattern::settle:
    return "settle";
  case GesturePattern::camera_move:
    return "camera_move";
  }
  return "unknown";
}

} // namespace instantee::direction
