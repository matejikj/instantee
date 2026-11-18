#include <instantee/direction/notebook_composition_templates.hpp>

#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "notebook_composition_templates", message};
}

[[nodiscard]] bool finite_positive(double v) noexcept {
  return std::isfinite(v) && v > 0.0;
}

[[nodiscard]] bool finite_nonneg(double v) noexcept {
  return std::isfinite(v) && v >= 0.0;
}

[[nodiscard]] double spacing_or(const StyleBible &bible, std::string_view id,
                                double fallback) {
  const auto it = bible.spacing().find(std::string(id));
  if (it == bible.spacing().end()) {
    return fallback;
  }
  return it->second.px;
}

[[nodiscard]] NotebookSlot make_slot(std::string id, std::string role, double x,
                                     double y, double w, double h,
                                     std::string content) {
  NotebookSlot s;
  s.id = std::move(id);
  s.role = std::move(role);
  s.x_px = x;
  s.y_px = y;
  s.width_px = w;
  s.height_px = h;
  s.content = std::move(content);
  return s;
}

[[nodiscard]] bool slot_inside(const NotebookSlot &s,
                               const ContentSafeRect &c) noexcept {
  constexpr double eps = 1e-6;
  return s.x_px + eps >= c.x_px && s.y_px + eps >= c.y_px &&
         s.x_px + s.width_px <= c.x_px + c.width_px + eps &&
         s.y_px + s.height_px <= c.y_px + c.height_px + eps;
}

[[nodiscard]] std::expected<std::vector<NotebookSlot>, diagnostics::Error>
build_slots(NotebookLayoutKind kind, const NotebookTopicData &topic,
            const ContentSafeRect &safe, double note_gap, double equation_pad,
            double step_stack) {
  std::vector<NotebookSlot> slots;
  const double x = safe.x_px;
  const double y = safe.y_px;
  const double w = safe.width_px;
  const double h = safe.height_px;
  if (!(w > 8.0) || !(h > 8.0)) {
    return std::unexpected(make_invalid("notebook.safe_area_too_small",
                                        "content safe rect is too small"));
  }

  switch (kind) {
  case NotebookLayoutKind::problem_statement: {
    if (topic.title.empty() || topic.problem_prompt.empty()) {
      return std::unexpected(make_invalid(
          "notebook.missing_problem", "problem layout needs title and prompt"));
    }
    const double title_h = std::min(80.0, h * 0.18);
    const double prompt_h = h - title_h - note_gap;
    slots.push_back(make_slot("title", "title", x, y, w, title_h, topic.title));
    slots.push_back(make_slot("prompt", "prompt", x, y + title_h + note_gap, w,
                              prompt_h, topic.problem_prompt));
    slots.push_back(make_slot("pin", "accessory", x + w - 36.0, y + 8.0, 28.0,
                              28.0, "pin"));
    break;
  }
  case NotebookLayoutKind::construction: {
    if (topic.construction_steps.empty()) {
      return std::unexpected(make_invalid(
          "notebook.missing_steps", "construction needs at least one step"));
    }
    slots.push_back(make_slot("title", "title", x, y, w, 56.0, topic.title));
    double cursor = y + 56.0 + note_gap;
    const double avail = h - 56.0 - note_gap;
    const double step_h = std::max(
        36.0, (avail - step_stack * static_cast<double>(
                                        topic.construction_steps.size() - 1)) /
                  static_cast<double>(topic.construction_steps.size()));
    for (std::size_t i = 0; i < topic.construction_steps.size(); ++i) {
      slots.push_back(make_slot("step-" + std::to_string(i + 1), "step", x,
                                cursor, w, step_h,
                                topic.construction_steps[i]));
      cursor += step_h + step_stack;
    }
    break;
  }
  case NotebookLayoutKind::derivation: {
    if (topic.derivation_equations.empty()) {
      return std::unexpected(
          make_invalid("notebook.missing_equations",
                       "derivation needs at least one equation"));
    }
    slots.push_back(make_slot("title", "title", x, y, w, 56.0, topic.title));
    double cursor = y + 56.0 + note_gap;
    const double avail = h - 56.0 - note_gap;
    const double eq_h = std::max(
        40.0,
        (avail - equation_pad * static_cast<double>(
                                    topic.derivation_equations.size() - 1)) /
            static_cast<double>(topic.derivation_equations.size()));
    for (std::size_t i = 0; i < topic.derivation_equations.size(); ++i) {
      slots.push_back(make_slot(
          "eq-" + std::to_string(i + 1), "equation", x + equation_pad * 0.25,
          cursor, w - equation_pad * 0.5, eq_h, topic.derivation_equations[i]));
      cursor += eq_h + equation_pad;
    }
    slots.push_back(make_slot("highlight", "accessory", x, y + h - 28.0,
                              w * 0.55, 20.0, "ink_highlight"));
    break;
  }
  case NotebookLayoutKind::visual_intuition: {
    if (topic.diagram_node_labels.size() < 2) {
      return std::unexpected(make_invalid(
          "notebook.missing_diagram",
          "visual intuition needs at least two diagram node labels"));
    }
    slots.push_back(make_slot("title", "title", x, y, w, 48.0, topic.title));
    const double diagram_h = h * 0.55;
    slots.push_back(make_slot("diagram", "diagram", x, y + 48.0 + note_gap, w,
                              diagram_h, topic.topic_id + ":diagram"));
    for (std::size_t i = 0; i < topic.diagram_node_labels.size(); ++i) {
      slots.push_back(make_slot("node-" + std::to_string(i + 1), "diagram",
                                x + 12.0 + static_cast<double>(i) * 72.0,
                                y + 64.0 + note_gap, 64.0, 28.0,
                                topic.diagram_node_labels[i]));
    }
    for (std::size_t i = 0; i < topic.diagram_edge_labels.size(); ++i) {
      slots.push_back(make_slot("edge-" + std::to_string(i + 1), "diagram",
                                x + 20.0 + static_cast<double>(i) * 80.0,
                                y + 100.0 + note_gap, 70.0, 18.0,
                                topic.diagram_edge_labels[i]));
    }
    double note_y = y + 48.0 + note_gap + diagram_h + note_gap;
    for (std::size_t i = 0; i < topic.intuition_notes.size(); ++i) {
      slots.push_back(make_slot("note-" + std::to_string(i + 1), "note", x,
                                note_y, w, 36.0, topic.intuition_notes[i]));
      note_y += 36.0 + note_gap * 0.5;
    }
    slots.push_back(make_slot("tape", "accessory", x + w * 0.35, y + 40.0, 48.0,
                              14.0, "tape_strip"));
    break;
  }
  case NotebookLayoutKind::summary: {
    if (topic.summary_claim.empty()) {
      return std::unexpected(
          make_invalid("notebook.missing_summary", "summary needs a claim"));
    }
    slots.push_back(make_slot("title", "title", x, y, w, 56.0, topic.title));
    slots.push_back(make_slot("claim", "claim", x, y + 56.0 + note_gap, w, 72.0,
                              topic.summary_claim));
    double cursor = y + 56.0 + note_gap + 72.0 + note_gap;
    for (std::size_t i = 0; i < topic.summary_bullets.size(); ++i) {
      slots.push_back(make_slot("bullet-" + std::to_string(i + 1), "bullet", x,
                                cursor, w, 40.0, topic.summary_bullets[i]));
      cursor += 40.0 + step_stack * 0.5;
    }
    break;
  }
  }

  for (const auto &s : slots) {
    if (!slot_inside(s, safe)) {
      return std::unexpected(
          make_invalid("notebook.slot_outside_safe_area",
                       "layout slot escaped the content safe rect"));
    }
  }
  return slots;
}

} // namespace

std::vector<NotebookLayoutSpec> default_notebook_layout_specs() noexcept {
  return {
      NotebookLayoutSpec{.kind = NotebookLayoutKind::problem_statement,
                         .duration_frames = 0,
                         .weight = 2},
      NotebookLayoutSpec{.kind = NotebookLayoutKind::construction,
                         .duration_frames = 0,
                         .weight = 3},
      NotebookLayoutSpec{.kind = NotebookLayoutKind::derivation,
                         .duration_frames = 0,
                         .weight = 4},
      NotebookLayoutSpec{.kind = NotebookLayoutKind::visual_intuition,
                         .duration_frames = 0,
                         .weight = 3},
      NotebookLayoutSpec{.kind = NotebookLayoutKind::summary,
                         .duration_frames = 0,
                         .weight = 2},
  };
}

NotebookTopicData example_pythagoras_topic() {
  NotebookTopicData t;
  t.topic_id = "pythagoras";
  t.title = "Pythagorean relation";
  t.problem_prompt = "Relate the sides of a right triangle.";
  t.construction_steps = {"Draw right triangle ABC with right angle at C.",
                          "Mark legs a, b and hypotenuse c."};
  t.derivation_equations = {"a^2 + b^2 = c^2", "c = \\sqrt{a^2 + b^2}"};
  t.diagram_node_labels = {"A", "B", "C"};
  t.diagram_edge_labels = {"a", "b", "c"};
  t.intuition_notes = {
      "Squares on the legs fill the square on the hypotenuse."};
  t.summary_claim = "In a right triangle, a^2 + b^2 = c^2.";
  t.summary_bullets = {"Works in Euclidean plane geometry.",
                       "Foundation for distance in R^2."};
  return t;
}

NotebookTopicData example_quadratic_topic() {
  NotebookTopicData t;
  t.topic_id = "quadratic-formula";
  t.title = "Quadratic formula";
  t.problem_prompt = "Solve ax^2 + bx + c = 0 for x.";
  t.construction_steps = {"Start from ax^2 + bx + c = 0 with a ≠ 0.",
                          "Complete the square / discriminant path."};
  t.derivation_equations = {"ax^2 + bx + c = 0",
                            "x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}"};
  t.diagram_node_labels = {"a", "b", "c", "D"};
  t.diagram_edge_labels = {"a->D", "b->D"};
  t.intuition_notes = {"Discriminant D = b^2 - 4ac counts real roots."};
  t.summary_claim = "Roots are (-b ± √D) / (2a).";
  t.summary_bullets = {"D>0 two roots, D=0 one root, D<0 none real.",
                       "Same template, different topic data."};
  return t;
}

NotebookCompositionRequest notebook_composition_request_from_style(
    const StyleBible &bible, NotebookTopicData topic, double canvas_width_px,
    double canvas_height_px) {
  NotebookCompositionRequest req;
  req.film_id = "notebook-" + topic.topic_id;
  req.canvas_width_px = canvas_width_px;
  req.canvas_height_px = canvas_height_px;
  req.safe_area = bible.safe_area();
  req.margin_px = spacing_or(bible, "margin", 48.0);
  req.note_gap_px = spacing_or(bible, "note_gap", 16.0);
  req.equation_pad_px = spacing_or(bible, "equation_pad", 20.0);
  req.step_stack_px = spacing_or(bible, "step_stack", 28.0);
  req.topic = std::move(topic);
  req.layouts = default_notebook_layout_specs();
  return req;
}

std::expected<NotebookCompositionAssembly, diagnostics::Error>
assemble_notebook_composition(const NotebookCompositionRequest &request) {
  if (request.topic.topic_id.empty() || request.topic.title.empty()) {
    return std::unexpected(make_invalid("notebook.missing_topic",
                                        "topic_id and title are required"));
  }
  if (!finite_positive(request.canvas_width_px) ||
      !finite_positive(request.canvas_height_px)) {
    return std::unexpected(make_invalid(
        "notebook.bad_canvas", "canvas dimensions must be finite and > 0"));
  }
  if (!finite_nonneg(request.margin_px) ||
      !finite_nonneg(request.note_gap_px) ||
      !finite_nonneg(request.equation_pad_px) ||
      !finite_nonneg(request.step_stack_px)) {
    return std::unexpected(make_invalid(
        "notebook.bad_spacing", "spacing values must be finite and >= 0"));
  }
  if (request.layouts.empty()) {
    return std::unexpected(make_invalid("notebook.empty_layouts",
                                        "at least one layout is required"));
  }

  auto safe = content_safe_rect(request.safe_area, request.canvas_width_px,
                                request.canvas_height_px);
  if (!safe) {
    return std::unexpected(safe.error());
  }

  bool all_absolute = true;
  std::uint64_t absolute_total = 0;
  for (const auto &spec : request.layouts) {
    if (spec.duration_frames == 0) {
      all_absolute = false;
      if (spec.weight == 0) {
        return std::unexpected(make_invalid(
            "notebook.bad_weight", "layout weight must be > 0 when packing"));
      }
    } else {
      absolute_total += spec.duration_frames;
    }
  }

  constexpr std::uint64_t kDefaultBudget = 900;
  const std::uint64_t budget =
      all_absolute ? absolute_total
                   : (absolute_total > 0 ? absolute_total : kDefaultBudget);
  if (budget == 0) {
    return std::unexpected(
        make_invalid("notebook.bad_budget", "total duration must be > 0"));
  }

  EditorialBeatScheduleRequest beat_req;
  beat_req.total_duration_frames = budget;
  for (const auto &spec : request.layouts) {
    const std::string kind = std::string{notebook_layout_kind_id(spec.kind)};
    beat_req.beats.push_back(EditorialBeatMarker{
        .kind = notebook_layout_beat_kind(spec.kind),
        .weight = all_absolute
                      ? std::max<std::uint64_t>(1, spec.duration_frames)
                      : spec.weight,
        .track_id = "notebook-" + kind,
        .target_id = kind + "-" + request.topic.topic_id,
        .property = "opacity",
    });
  }
  auto beats = schedule_editorial_beats(beat_req);
  if (!beats) {
    return std::unexpected(beats.error());
  }

  NotebookCompositionAssembly out;
  out.film_id = request.film_id;
  out.topic_id = request.topic.topic_id;
  out.has_audio = false;
  out.beat_schedule = std::move(*beats);
  out.total_duration_frames = out.beat_schedule.total_duration_frames;

  if (out.beat_schedule.beats.size() != request.layouts.size()) {
    return std::unexpected(make_invalid("notebook.beat_mismatch",
                                        "beat count must match layout count"));
  }

  for (std::size_t i = 0; i < request.layouts.size(); ++i) {
    const auto &spec = request.layouts[i];
    const auto &beat = out.beat_schedule.beats[i];
    auto slots =
        build_slots(spec.kind, request.topic, *safe, request.note_gap_px,
                    request.equation_pad_px, request.step_stack_px);
    if (!slots) {
      return std::unexpected(slots.error());
    }
    NotebookLayoutInstance inst;
    inst.kind = spec.kind;
    inst.shot_id = std::string{notebook_layout_kind_id(spec.kind)} + "-" +
                   request.topic.topic_id;
    inst.start_frame = beat.start_frame;
    inst.duration_frames = beat.duration_frames;
    inst.beat_kind = notebook_layout_beat_kind(spec.kind);
    inst.intent_kind = notebook_layout_intent_kind(spec.kind);
    inst.slots = std::move(*slots);
    out.layouts.push_back(std::move(inst));
  }
  return out;
}

std::expected<NotebookCompositionAssembly, diagnostics::Error>
assemble_notebook_composition_from_style(const StyleBible &bible,
                                         NotebookTopicData topic,
                                         double canvas_width_px,
                                         double canvas_height_px) {
  auto req = notebook_composition_request_from_style(
      bible, std::move(topic), canvas_width_px, canvas_height_px);
  auto assembled = assemble_notebook_composition(req);
  if (!assembled) {
    return assembled;
  }
  assembled->style_package_id = std::string{bible.package_id()};
  assembled->style_version = std::string{bible.version()};
  return assembled;
}

std::string format_notebook_composition_example(
    std::string_view label, const NotebookCompositionAssembly &assembly) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);
  oss << "notebook-template " << label << '\n';
  oss << "topic=" << assembly.topic_id << " film=" << assembly.film_id
      << " style=" << assembly.style_package_id << "@" << assembly.style_version
      << '\n';
  oss << "layouts=" << assembly.layouts.size()
      << " frames=" << assembly.total_duration_frames
      << " audio=" << (assembly.has_audio ? 1 : 0) << '\n';
  for (const auto &layout : assembly.layouts) {
    oss << "layout " << notebook_layout_kind_id(layout.kind)
        << " shot=" << layout.shot_id << " start=" << layout.start_frame
        << " dur=" << layout.duration_frames
        << " beat=" << static_cast<int>(layout.beat_kind)
        << " intent=" << intent_kind_id(layout.intent_kind)
        << " slots=" << layout.slots.size() << '\n';
    for (const auto &slot : layout.slots) {
      oss << "  slot " << slot.id << " role=" << slot.role
          << " xywh=" << slot.x_px << "," << slot.y_px << "," << slot.width_px
          << "x" << slot.height_px << " content=" << slot.content << '\n';
    }
  }
  return oss.str();
}

const NotebookSlot *notebook_layout_slot(const NotebookLayoutInstance &layout,
                                         std::string_view slot_id) noexcept {
  for (const auto &slot : layout.slots) {
    if (slot.id == slot_id) {
      return &slot;
    }
  }
  return nullptr;
}

} // namespace instantee::direction
