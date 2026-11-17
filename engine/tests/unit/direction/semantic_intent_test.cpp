#include <instantee/direction/semantic_intent.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>

namespace {

using instantee::direction::intent_kind_id;
using instantee::direction::IntentKind;
using instantee::direction::make_semantic_intent;
using instantee::direction::parse_intent_kind;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  const std::array<std::string_view, 3> known{"headline", "panel_a", "panel_b"};

  auto reveal = make_semantic_intent(
      "reveal", std::array<std::string_view, 1>{"headline"}, 0.8, 0.9,
      std::optional<std::uint32_t>{24}, known);
  expect(reveal.has_value(), "valid reveal");
  if (reveal) {
    expect(reveal->kind == IntentKind::reveal, "reveal kind");
    expect(reveal->targets.size() == 1, "reveal targets");
    expect(reveal->targets.front().value() == "headline", "reveal target id");
    expect(reveal->duration_frames == 24U, "reveal duration");
  }

  auto compare = make_semantic_intent(
      IntentKind::compare,
      std::array<std::string_view, 2>{"panel_a", "panel_b"}, 0.5, 0.7,
      std::nullopt, known);
  expect(compare.has_value(), "valid compare");

  auto unknown_kind = parse_intent_kind("teleport");
  expect(!unknown_kind.has_value(), "reject unknown kind");
  if (!unknown_kind) {
    expect(unknown_kind.error().code == "unknown_intent_kind", "kind code");
  }

  auto bad_intensity = make_semantic_intent(
      IntentKind::focus, std::array<std::string_view, 1>{"headline"}, 1.5, 0.5,
      std::nullopt, known);
  expect(!bad_intensity.has_value(), "reject intensity > 1");
  if (!bad_intensity) {
    expect(bad_intensity.error().code == "invalid_intent_intensity",
           "intensity code");
  }

  auto nan_importance = make_semantic_intent(
      IntentKind::explain, std::array<std::string_view, 1>{"headline"}, 0.2,
      std::numeric_limits<double>::quiet_NaN(), std::nullopt, known);
  expect(!nan_importance.has_value(), "reject non-finite importance");

  auto bad_duration = make_semantic_intent(
      IntentKind::build_suspense, std::array<std::string_view, 1>{"headline"},
      0.4, 0.6, std::optional<std::uint32_t>{0}, known);
  expect(!bad_duration.has_value(), "reject zero duration");
  if (!bad_duration) {
    expect(bad_duration.error().code == "invalid_intent_duration",
           "duration code");
  }

  auto unknown_target = make_semantic_intent(
      IntentKind::reveal, std::array<std::string_view, 1>{"missing_node"}, 0.5,
      0.5, std::nullopt, known);
  expect(!unknown_target.has_value(), "reject unknown target");
  if (!unknown_target) {
    expect(unknown_target.error().code == "unknown_intent_target",
           "unknown target code");
  }

  auto too_few = make_semantic_intent(
      IntentKind::compare, std::array<std::string_view, 1>{"panel_a"}, 0.5, 0.5,
      std::nullopt, known);
  expect(!too_few.has_value(), "compare needs two targets");
  if (!too_few) {
    expect(too_few.error().code == "insufficient_intent_targets", "arity code");
  }

  auto duplicate = make_semantic_intent(
      IntentKind::compare,
      std::array<std::string_view, 2>{"panel_a", "panel_a"}, 0.5, 0.5,
      std::nullopt, known);
  expect(!duplicate.has_value(), "reject duplicate targets");
  if (!duplicate) {
    expect(duplicate.error().code == "duplicate_intent_target",
           "duplicate code");
  }

  expect(intent_kind_id(IntentKind::build_suspense) == "build_suspense",
         "kind id");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
