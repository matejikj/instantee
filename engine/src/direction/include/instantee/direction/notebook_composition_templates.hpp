#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/editorial_beats.hpp>
#include <instantee/direction/safe_area.hpp>
#include <instantee/direction/semantic_intent.hpp>
#include <instantee/direction/style_bible.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Scientific-notebook composition templates. Layouts are fixed skeletons;
// mathematical topic content is injected as data so a new subject is expressed
// by swapping topic fields without rewriting the template or renderer.

enum class NotebookLayoutKind : std::uint8_t {
  problem_statement = 1,
  construction = 2,
  derivation = 3,
  visual_intuition = 4,
  summary = 5,
};

struct NotebookTopicData {
  std::string topic_id{};
  std::string title{};
  std::string problem_prompt{};
  std::vector<std::string> construction_steps{};
  // TeX-like equation sources transported as opaque author strings.
  std::vector<std::string> derivation_equations{};
  std::vector<std::string> diagram_node_labels{};
  std::vector<std::string> diagram_edge_labels{};
  std::vector<std::string> intuition_notes{};
  std::string summary_claim{};
  std::vector<std::string> summary_bullets{};
};

struct NotebookLayoutSpec {
  NotebookLayoutKind kind = NotebookLayoutKind::problem_statement;
  // Absolute frames; when 0 the assembler packs relative weights.
  std::uint64_t duration_frames = 0;
  std::uint64_t weight = 1;
};

struct NotebookCompositionRequest {
  std::string film_id{"notebook-explainer"};
  double canvas_width_px = 1080.0;
  double canvas_height_px = 1920.0;
  SafeAreaPolicy safe_area{};
  double margin_px = 48.0;
  double note_gap_px = 16.0;
  double equation_pad_px = 20.0;
  double step_stack_px = 28.0;
  NotebookTopicData topic{};
  std::vector<NotebookLayoutSpec> layouts{};
};

struct NotebookSlot {
  std::string id{};
  std::string
      role{}; // title|prompt|step|equation|diagram|note|claim|bullet|accessory
  double x_px = 0;
  double y_px = 0;
  double width_px = 0;
  double height_px = 0;
  // Content bound from topic data for this assembly.
  std::string content{};
};

struct NotebookLayoutInstance {
  NotebookLayoutKind kind = NotebookLayoutKind::problem_statement;
  std::string shot_id{};
  std::uint64_t start_frame = 0;
  std::uint64_t duration_frames = 0;
  EditorialBeatKind beat_kind = EditorialBeatKind::hook;
  IntentKind intent_kind = IntentKind::reveal;
  std::vector<NotebookSlot> slots{};
};

struct NotebookCompositionAssembly {
  std::string film_id{};
  std::string style_package_id{};
  std::string style_version{};
  std::string topic_id{};
  std::uint64_t total_duration_frames = 0;
  std::vector<NotebookLayoutInstance> layouts{};
  EditorialBeatSchedule beat_schedule{};
  bool has_audio = false;
};

[[nodiscard]] constexpr std::string_view
notebook_layout_kind_id(NotebookLayoutKind kind) noexcept {
  switch (kind) {
  case NotebookLayoutKind::problem_statement:
    return "problem_statement";
  case NotebookLayoutKind::construction:
    return "construction";
  case NotebookLayoutKind::derivation:
    return "derivation";
  case NotebookLayoutKind::visual_intuition:
    return "visual_intuition";
  case NotebookLayoutKind::summary:
    return "summary";
  }
  return "unknown";
}

[[nodiscard]] constexpr EditorialBeatKind
notebook_layout_beat_kind(NotebookLayoutKind kind) noexcept {
  switch (kind) {
  case NotebookLayoutKind::problem_statement:
    return EditorialBeatKind::hook;
  case NotebookLayoutKind::construction:
    return EditorialBeatKind::claim;
  case NotebookLayoutKind::derivation:
  case NotebookLayoutKind::visual_intuition:
    return EditorialBeatKind::evidence;
  case NotebookLayoutKind::summary:
    return EditorialBeatKind::conclusion;
  }
  return EditorialBeatKind::hook;
}

[[nodiscard]] constexpr IntentKind
notebook_layout_intent_kind(NotebookLayoutKind kind) noexcept {
  switch (kind) {
  case NotebookLayoutKind::problem_statement:
    return IntentKind::reveal;
  case NotebookLayoutKind::construction:
    return IntentKind::focus;
  case NotebookLayoutKind::derivation:
    return IntentKind::explain;
  case NotebookLayoutKind::visual_intuition:
    return IntentKind::compare;
  case NotebookLayoutKind::summary:
    return IntentKind::focus;
  }
  return IntentKind::reveal;
}

// Default five-layout notebook reel order.
[[nodiscard]] std::vector<NotebookLayoutSpec>
default_notebook_layout_specs() noexcept;

// Example topic payloads used by template examples / tests.
[[nodiscard]] NotebookTopicData example_pythagoras_topic();
[[nodiscard]] NotebookTopicData example_quadratic_topic();

// Fill spacing + safe-area from a Style Bible (scientific-notebook tokens).
[[nodiscard]] NotebookCompositionRequest
notebook_composition_request_from_style(const StyleBible &bible,
                                        NotebookTopicData topic,
                                        double canvas_width_px = 1080.0,
                                        double canvas_height_px = 1920.0);

// Assemble layouts from template skeletons + topic data. Does not call into
// render-core; accessories/diagrams are referenced by slot roles only.
[[nodiscard]] std::expected<NotebookCompositionAssembly, diagnostics::Error>
assemble_notebook_composition(const NotebookCompositionRequest &request);

[[nodiscard]] std::expected<NotebookCompositionAssembly, diagnostics::Error>
assemble_notebook_composition_from_style(const StyleBible &bible,
                                         NotebookTopicData topic,
                                         double canvas_width_px = 1080.0,
                                         double canvas_height_px = 1920.0);

// Deterministic text dump for template examples.
[[nodiscard]] std::string format_notebook_composition_example(
    std::string_view label, const NotebookCompositionAssembly &assembly);

[[nodiscard]] const NotebookSlot *
notebook_layout_slot(const NotebookLayoutInstance &layout,
                     std::string_view slot_id) noexcept;

} // namespace instantee::direction
