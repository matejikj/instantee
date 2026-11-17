#include <instantee/direction/composition_constraints.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using instantee::direction::CompositionConstraint;
using instantee::direction::CompositionConstraintKind;
using instantee::direction::ConstraintSeverity;
using instantee::direction::LayoutElement;
using instantee::direction::solve_composition;
using instantee::direction::TextMetrics;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::string
fingerprint(const instantee::direction::CompositionSolution &solution) {
  std::string out;
  for (const auto &el : solution.elements) {
    out.append(el.id);
    out.push_back('@');
    out.append(std::to_string(el.x_px));
    out.push_back(',');
    out.append(std::to_string(el.y_px));
    out.push_back(';');
  }
  for (const auto &w : solution.warnings) {
    out.append(w.code);
    out.push_back(':');
    out.append(w.element_id);
    out.push_back('|');
  }
  return out;
}

} // namespace

int main() {
  // Hard keep-visible: clamp into canvas.
  {
    LayoutElement badge{.id = "badge",
                        .x_px = -40,
                        .y_px = 10,
                        .width_px = 80,
                        .height_px = 40,
                        .importance = 0.9,
                        .keep_visible = true};
    CompositionConstraint c{.kind = CompositionConstraintKind::keep_visible,
                            .severity = ConstraintSeverity::hard,
                            .element_id = "badge"};
    auto a = solve_composition(std::array{badge}, std::array{c}, 1080, 1920);
    auto b = solve_composition(std::array{badge}, std::array{c}, 1080, 1920);
    expect(a.has_value() && b.has_value(), "keep-visible solves");
    if (a && b) {
      expect(a->elements.front().x_px == 0.0, "clamped to left");
      expect(fingerprint(*a) == fingerprint(*b), "deterministic");
    }
  }

  // Unsatisfiable hard: element larger than canvas.
  {
    LayoutElement huge{.id = "huge",
                       .x_px = 0,
                       .y_px = 0,
                       .width_px = 2000,
                       .height_px = 100,
                       .importance = 1.0};
    CompositionConstraint c{.kind = CompositionConstraintKind::bound_inside,
                            .severity = ConstraintSeverity::hard,
                            .element_id = "huge"};
    auto failed =
        solve_composition(std::array{huge}, std::array{c}, 1080, 1920);
    expect(!failed.has_value(), "oversized hard fails");
    if (!failed) {
      expect(failed.error().code == "unsatisfiable_hard_constraint",
             "hard error code");
    }
  }

  // Soft text legibility warning with injected metrics.
  {
    LayoutElement text{.id = "caption",
                       .x_px = 10,
                       .y_px = 10,
                       .width_px = 200,
                       .height_px = 18,
                       .importance = 0.4,
                       .text = TextMetrics{.width_px = 180,
                                           .height_px = 16,
                                           .ascent_px = 12,
                                           .descent_px = 4}};
    CompositionConstraint c{.kind = CompositionConstraintKind::text_legibility,
                            .severity = ConstraintSeverity::soft,
                            .element_id = "caption",
                            .min_text_height_px = 24.0};
    auto soft = solve_composition(std::array{text}, std::array{c}, 1080, 1920);
    expect(soft.has_value(), "soft illegible warns");
    if (soft) {
      expect(soft->warnings.size() == 1, "one warning");
      expect(soft->warnings.front().code == "soft_text_illegible",
             "legibility warning code");
    }
  }

  // Hard text legibility fails.
  {
    LayoutElement text{.id = "tiny",
                       .x_px = 0,
                       .y_px = 0,
                       .width_px = 50,
                       .height_px = 10,
                       .text = TextMetrics{.width_px = 40, .height_px = 10}};
    CompositionConstraint c{.kind = CompositionConstraintKind::text_legibility,
                            .severity = ConstraintSeverity::hard,
                            .element_id = "tiny",
                            .min_text_height_px = 24.0};
    auto failed =
        solve_composition(std::array{text}, std::array{c}, 1080, 1920);
    expect(!failed.has_value(), "hard illegible fails");
  }

  // Hierarchy containment.
  {
    LayoutElement parent{.id = "panel",
                         .x_px = 100,
                         .y_px = 100,
                         .width_px = 200,
                         .height_px = 200};
    LayoutElement child{.id = "icon",
                        .parent_id = "panel",
                        .x_px = 50,
                        .y_px = 50,
                        .width_px = 40,
                        .height_px = 40};
    CompositionConstraint c{
        .kind = CompositionConstraintKind::hierarchy_containment,
        .severity = ConstraintSeverity::hard,
        .element_id = "icon",
        .other_id = "panel"};
    auto solved =
        solve_composition(std::array{parent, child}, std::array{c}, 1080, 1920);
    expect(solved.has_value(), "hierarchy solves");
    if (solved) {
      const auto &icon = solved->elements.back().id == "icon"
                             ? solved->elements.back()
                             : solved->elements.front();
      expect(icon.x_px >= 100.0 && icon.y_px >= 100.0, "child inside parent");
    }
  }

  // Soft overlap warning (no move).
  {
    LayoutElement a{.id = "a",
                    .x_px = 0,
                    .y_px = 0,
                    .width_px = 50,
                    .height_px = 50,
                    .importance = 0.2};
    LayoutElement b{.id = "b",
                    .x_px = 20,
                    .y_px = 10,
                    .width_px = 50,
                    .height_px = 50,
                    .importance = 0.9};
    CompositionConstraint c{.kind =
                                CompositionConstraintKind::importance_overlap,
                            .severity = ConstraintSeverity::soft,
                            .element_id = "a",
                            .other_id = "b"};
    auto soft = solve_composition(std::array{a, b}, std::array{c}, 1080, 1920);
    expect(soft.has_value() && !soft->warnings.empty(), "soft overlap warns");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
