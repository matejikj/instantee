#include <instantee/direction/motion_grammar.hpp>

#include <algorithm>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "motion_grammar", message};
}

void add_or_die(MotionGrammarRegistry &registry, MotionGrammarBinding binding) {
  auto result = registry.register_binding(std::move(binding));
  (void)result;
}

} // namespace

std::string MotionGrammarRegistry::key_for(std::string_view package_id,
                                           IntentKind intent) {
  return std::string(package_id) + "/" + std::string(intent_kind_id(intent));
}

std::expected<void, diagnostics::Error>
MotionGrammarRegistry::register_binding(MotionGrammarBinding binding) {
  if (binding.package_id.empty()) {
    return std::unexpected(make_invalid("invalid_package_id",
                                        "Motion grammar package_id is empty"));
  }
  if (binding.property.empty()) {
    return std::unexpected(
        make_invalid("invalid_property", "Motion grammar property is empty"));
  }
  if (!(binding.duration_scale > 0.0 && binding.duration_scale <= 1.0)) {
    return std::unexpected(make_invalid("invalid_duration_scale",
                                        "duration_scale must be in (0, 1]"));
  }
  if (!(binding.intensity_scale > 0.0)) {
    return std::unexpected(
        make_invalid("invalid_intensity_scale", "intensity_scale must be > 0"));
  }
  const std::string key = key_for(binding.package_id, binding.intent);
  const auto [it, inserted] = by_key_.emplace(key, std::move(binding));
  if (!inserted) {
    return std::unexpected(make_invalid(
        "duplicate_motion_grammar",
        "Motion grammar binding already registered for package/intent"));
  }
  (void)it;
  return {};
}

bool MotionGrammarRegistry::contains(std::string_view package_id,
                                     IntentKind intent) const {
  return by_key_.contains(key_for(package_id, intent));
}

std::expected<MotionGrammarBinding, diagnostics::Error>
MotionGrammarRegistry::resolve(std::string_view package_id,
                               IntentKind intent) const {
  const auto it = by_key_.find(key_for(package_id, intent));
  if (it == by_key_.end()) {
    return std::unexpected(
        make_invalid("unknown_motion_grammar",
                     "No motion grammar binding for package/intent"));
  }
  return it->second;
}

std::vector<std::string> MotionGrammarRegistry::package_ids() const {
  std::vector<std::string> ids;
  for (const auto &entry : by_key_) {
    const auto slash = entry.second.package_id;
    if (std::find(ids.begin(), ids.end(), slash) == ids.end()) {
      ids.push_back(slash);
    }
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

MotionGrammarRegistry make_builtin_motion_grammar_registry() {
  MotionGrammarRegistry registry;

  // editorial — punchy opacity reveals, tight stagger, short settle.
  add_or_die(registry, {.package_id = "editorial",
                        .intent = IntentKind::reveal,
                        .pattern = GesturePattern::reveal,
                        .ease = MotionEase::ease_out,
                        .duration_scale = 0.35,
                        .intensity_scale = 1.0,
                        .stagger_frames = 2,
                        .property = "opacity",
                        .from_value = 0.0});
  add_or_die(registry, {.package_id = "editorial",
                        .intent = IntentKind::explain,
                        .pattern = GesturePattern::reveal,
                        .ease = MotionEase::ease_out,
                        .duration_scale = 0.45,
                        .intensity_scale = 0.9,
                        .stagger_frames = 2,
                        .property = "opacity",
                        .from_value = 0.0});
  add_or_die(registry, {.package_id = "editorial",
                        .intent = IntentKind::compare,
                        .pattern = GesturePattern::stagger,
                        .ease = MotionEase::ease_in_out,
                        .duration_scale = 0.55,
                        .intensity_scale = 1.0,
                        .stagger_frames = 3,
                        .property = "opacity",
                        .from_value = 0.0});
  add_or_die(registry, {.package_id = "editorial",
                        .intent = IntentKind::focus,
                        .pattern = GesturePattern::settle,
                        .ease = MotionEase::spring,
                        .duration_scale = 0.4,
                        .intensity_scale = 1.0,
                        .stagger_frames = 0,
                        .property = "opacity",
                        .from_value = 0.35,
                        .has_hold = true,
                        .hold_value = 0.7,
                        .hold_fraction = 0.4});
  add_or_die(registry, {.package_id = "editorial",
                        .intent = IntentKind::build_suspense,
                        .pattern = GesturePattern::camera_move,
                        .ease = MotionEase::ease_in,
                        .duration_scale = 0.7,
                        .intensity_scale = 1.0,
                        .stagger_frames = 0,
                        .property = "opacity",
                        .from_value = 0.15,
                        .has_hold = true,
                        .hold_value = 0.25,
                        .hold_fraction = 0.55});

  // paper-cinema — slower folds, larger stagger, spring settle.
  add_or_die(registry, {.package_id = "paper-cinema",
                        .intent = IntentKind::reveal,
                        .pattern = GesturePattern::reveal,
                        .ease = MotionEase::ease_in_out,
                        .duration_scale = 0.55,
                        .intensity_scale = 0.85,
                        .stagger_frames = 4,
                        .property = "opacity",
                        .from_value = 0.0});
  add_or_die(registry, {.package_id = "paper-cinema",
                        .intent = IntentKind::explain,
                        .pattern = GesturePattern::reveal,
                        .ease = MotionEase::ease_in_out,
                        .duration_scale = 0.6,
                        .intensity_scale = 0.8,
                        .stagger_frames = 4,
                        .property = "opacity",
                        .from_value = 0.0});
  add_or_die(registry, {.package_id = "paper-cinema",
                        .intent = IntentKind::compare,
                        .pattern = GesturePattern::stagger,
                        .ease = MotionEase::ease_in_out,
                        .duration_scale = 0.7,
                        .intensity_scale = 0.9,
                        .stagger_frames = 6,
                        .property = "opacity",
                        .from_value = 0.0});
  add_or_die(registry, {.package_id = "paper-cinema",
                        .intent = IntentKind::focus,
                        .pattern = GesturePattern::settle,
                        .ease = MotionEase::spring,
                        .duration_scale = 0.6,
                        .intensity_scale = 0.95,
                        .stagger_frames = 0,
                        .property = "opacity",
                        .from_value = 0.2,
                        .has_hold = true,
                        .hold_value = 0.55,
                        .hold_fraction = 0.5});
  add_or_die(registry, {.package_id = "paper-cinema",
                        .intent = IntentKind::build_suspense,
                        .pattern = GesturePattern::camera_move,
                        .ease = MotionEase::ease_in_out,
                        .duration_scale = 0.85,
                        .intensity_scale = 0.9,
                        .stagger_frames = 0,
                        .property = "opacity",
                        .from_value = 0.1,
                        .has_hold = true,
                        .hold_value = 0.2,
                        .hold_fraction = 0.65});

  // scientific-notebook — measured ink reveals, modest stagger.
  add_or_die(registry, {.package_id = "scientific-notebook",
                        .intent = IntentKind::reveal,
                        .pattern = GesturePattern::reveal,
                        .ease = MotionEase::linear,
                        .duration_scale = 0.5,
                        .intensity_scale = 0.75,
                        .stagger_frames = 1,
                        .property = "opacity",
                        .from_value = 0.0});
  add_or_die(registry, {.package_id = "scientific-notebook",
                        .intent = IntentKind::explain,
                        .pattern = GesturePattern::reveal,
                        .ease = MotionEase::linear,
                        .duration_scale = 0.65,
                        .intensity_scale = 0.7,
                        .stagger_frames = 1,
                        .property = "opacity",
                        .from_value = 0.0});
  add_or_die(registry, {.package_id = "scientific-notebook",
                        .intent = IntentKind::compare,
                        .pattern = GesturePattern::stagger,
                        .ease = MotionEase::ease_out,
                        .duration_scale = 0.6,
                        .intensity_scale = 0.8,
                        .stagger_frames = 2,
                        .property = "opacity",
                        .from_value = 0.0});
  add_or_die(registry, {.package_id = "scientific-notebook",
                        .intent = IntentKind::focus,
                        .pattern = GesturePattern::settle,
                        .ease = MotionEase::ease_out,
                        .duration_scale = 0.45,
                        .intensity_scale = 0.85,
                        .stagger_frames = 0,
                        .property = "opacity",
                        .from_value = 0.4,
                        .has_hold = true,
                        .hold_value = 0.75,
                        .hold_fraction = 0.35});
  add_or_die(registry, {.package_id = "scientific-notebook",
                        .intent = IntentKind::build_suspense,
                        .pattern = GesturePattern::camera_move,
                        .ease = MotionEase::ease_in,
                        .duration_scale = 0.75,
                        .intensity_scale = 0.8,
                        .stagger_frames = 0,
                        .property = "opacity",
                        .from_value = 0.2,
                        .has_hold = true,
                        .hold_value = 0.35,
                        .hold_fraction = 0.5});

  return registry;
}

} // namespace instantee::direction
