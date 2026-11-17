#include <instantee/direction/semantic_intent.hpp>

#include <cmath>
#include <string>
#include <unordered_set>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_invalid(std::string_view code, std::string_view message,
                                std::string json_path = {}) {
  auto out = diagnostics::Error{diagnostics::ExitCode::invalid_input,
                                diagnostics::ErrorKind::invalid_input, code,
                                "semantic_intent", message};
  if (!json_path.empty()) {
    out = std::move(out).with_json_path(std::move(json_path));
  }
  return out;
}

[[nodiscard]] bool finite_unit_interval(double value) noexcept {
  return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

[[nodiscard]] std::size_t minimum_targets(IntentKind kind) noexcept {
  switch (kind) {
  case IntentKind::compare:
    return 2;
  case IntentKind::reveal:
  case IntentKind::focus:
  case IntentKind::explain:
  case IntentKind::build_suspense:
    return 1;
  }
  return 1;
}

constexpr std::uint32_t kMaxDurationFrames = 1'000'000U;

} // namespace

std::expected<IntentKind, diagnostics::Error>
parse_intent_kind(std::string_view text) {
  if (text == "reveal") {
    return IntentKind::reveal;
  }
  if (text == "focus") {
    return IntentKind::focus;
  }
  if (text == "compare") {
    return IntentKind::compare;
  }
  if (text == "explain") {
    return IntentKind::explain;
  }
  if (text == "build_suspense") {
    return IntentKind::build_suspense;
  }
  return std::unexpected(make_invalid("unknown_intent_kind",
                                      "Unsupported semantic intent kind",
                                      "/intent/type"));
}

std::expected<SemanticIntent, diagnostics::Error>
make_semantic_intent(IntentKind kind,
                     std::span<const std::string_view> target_ids,
                     double intensity, double importance,
                     std::optional<std::uint32_t> duration_frames,
                     std::span<const std::string_view> known_node_ids) {
  if (!finite_unit_interval(intensity)) {
    return std::unexpected(
        make_invalid("invalid_intent_intensity",
                     "Intent intensity must be a finite value in [0, 1]",
                     "/intent/intensity"));
  }
  if (!finite_unit_interval(importance)) {
    return std::unexpected(
        make_invalid("invalid_intent_importance",
                     "Intent importance must be a finite value in [0, 1]",
                     "/intent/importance"));
  }
  if (duration_frames.has_value()) {
    if (*duration_frames < 1U || *duration_frames > kMaxDurationFrames) {
      return std::unexpected(
          make_invalid("invalid_intent_duration",
                       "Intent duration_frames must be in [1, 1000000]",
                       "/intent/duration_frames"));
    }
  }

  std::unordered_set<std::string_view> known;
  known.reserve(known_node_ids.size());
  for (const std::string_view id : known_node_ids) {
    if (id.empty()) {
      return std::unexpected(make_invalid(
          "invalid_known_node_id", "Known node id set contains an empty id"));
    }
    known.insert(id);
  }

  const std::size_t min_targets = minimum_targets(kind);
  if (target_ids.size() < min_targets) {
    return std::unexpected(make_invalid(
        "insufficient_intent_targets",
        "Intent does not name enough target node ids", "/intent/target_ids"));
  }

  SemanticIntent intent;
  intent.kind = kind;
  intent.intensity = intensity;
  intent.importance = importance;
  intent.duration_frames = duration_frames;
  intent.targets.reserve(target_ids.size());

  std::unordered_set<std::string_view> seen;
  seen.reserve(target_ids.size());
  for (std::size_t index = 0; index < target_ids.size(); ++index) {
    const std::string_view raw = target_ids[index];
    if (raw.empty()) {
      return std::unexpected(make_invalid(
          "invalid_intent_target", "Intent target id must be non-empty",
          "/intent/target_ids/" + std::to_string(index)));
    }
    if (!seen.insert(raw).second) {
      return std::unexpected(make_invalid(
          "duplicate_intent_target", "Intent target ids must be unique",
          "/intent/target_ids/" + std::to_string(index)));
    }
    if (!known.contains(raw)) {
      return std::unexpected(
          make_invalid("unknown_intent_target",
                       "Intent target id is not present in the shot node set",
                       "/intent/target_ids/" + std::to_string(index)));
    }
    auto node = core::NodeId::make(raw);
    if (!node) {
      return std::unexpected(std::move(node.error()));
    }
    intent.targets.push_back(std::move(*node));
  }

  return intent;
}

std::expected<SemanticIntent, diagnostics::Error>
make_semantic_intent(std::string_view kind_text,
                     std::span<const std::string_view> target_ids,
                     double intensity, double importance,
                     std::optional<std::uint32_t> duration_frames,
                     std::span<const std::string_view> known_node_ids) {
  auto kind = parse_intent_kind(kind_text);
  if (!kind) {
    return std::unexpected(kind.error());
  }
  return make_semantic_intent(*kind, target_ids, intensity, importance,
                              duration_frames, known_node_ids);
}

} // namespace instantee::direction
