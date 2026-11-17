#include <instantee/direction/composition_constraints.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_error(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "composition_constraints", message};
}

struct Rect {
  double x = 0;
  double y = 0;
  double w = 0;
  double h = 0;

  [[nodiscard]] double right() const noexcept { return x + w; }
  [[nodiscard]] double bottom() const noexcept { return y + h; }
};

[[nodiscard]] Rect rect_of(const LayoutElement &el) noexcept {
  return Rect{el.x_px, el.y_px, el.width_px, el.height_px};
}

[[nodiscard]] bool overlaps(const Rect &a, const Rect &b) noexcept {
  return a.x < b.right() && b.x < a.right() && a.y < b.bottom() &&
         b.y < a.bottom();
}

[[nodiscard]] LayoutElement *find_mutable(std::vector<LayoutElement> &elements,
                                          std::string_view id) {
  for (LayoutElement &el : elements) {
    if (el.id == id) {
      return &el;
    }
  }
  return nullptr;
}

[[nodiscard]] const LayoutElement *
find_const(const std::vector<LayoutElement> &elements, std::string_view id) {
  for (const LayoutElement &el : elements) {
    if (el.id == id) {
      return &el;
    }
  }
  return nullptr;
}

void clamp_inside(LayoutElement &el, double canvas_w, double canvas_h) {
  if (el.width_px > canvas_w || el.height_px > canvas_h) {
    return; // unsatisfiable size; detected as hard failure later
  }
  el.x_px = std::clamp(el.x_px, 0.0, canvas_w - el.width_px);
  el.y_px = std::clamp(el.y_px, 0.0, canvas_h - el.height_px);
}

void contain_in_parent(LayoutElement &child, const LayoutElement &parent) {
  child.x_px = std::clamp(child.x_px, parent.x_px,
                          parent.x_px + parent.width_px - child.width_px);
  child.y_px = std::clamp(child.y_px, parent.y_px,
                          parent.y_px + parent.height_px - child.height_px);
}

[[nodiscard]] bool inside(const LayoutElement &el, double canvas_w,
                          double canvas_h) noexcept {
  return el.x_px >= 0.0 && el.y_px >= 0.0 &&
         el.x_px + el.width_px <= canvas_w + 1e-9 &&
         el.y_px + el.height_px <= canvas_h + 1e-9;
}

[[nodiscard]] bool contained(const LayoutElement &child,
                             const LayoutElement &parent) noexcept {
  return child.x_px >= parent.x_px - 1e-9 && child.y_px >= parent.y_px - 1e-9 &&
         child.x_px + child.width_px <= parent.x_px + parent.width_px + 1e-9 &&
         child.y_px + child.height_px <= parent.y_px + parent.height_px + 1e-9;
}

void push_apart(LayoutElement &low, LayoutElement &high) {
  // Deterministic nudge: move the lower-importance element right, else down.
  const Rect a = rect_of(low);
  const Rect b = rect_of(high);
  const double overlap_x = std::min(a.right(), b.right()) - std::max(a.x, b.x);
  const double overlap_y =
      std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y);
  if (overlap_x <= 0.0 || overlap_y <= 0.0) {
    return;
  }
  if (overlap_x <= overlap_y) {
    low.x_px = high.x_px + high.width_px;
  } else {
    low.y_px = high.y_px + high.height_px;
  }
}

void sort_elements(std::vector<LayoutElement> &elements) {
  std::sort(elements.begin(), elements.end(),
            [](const LayoutElement &a, const LayoutElement &b) {
              return a.id < b.id;
            });
}

void sort_warnings(std::vector<CompositionWarning> &warnings) {
  std::sort(warnings.begin(), warnings.end(),
            [](const CompositionWarning &a, const CompositionWarning &b) {
              if (a.element_id != b.element_id) {
                return a.element_id < b.element_id;
              }
              if (a.code != b.code) {
                return a.code < b.code;
              }
              return a.message < b.message;
            });
  warnings.erase(
      std::unique(warnings.begin(), warnings.end(),
                  [](const CompositionWarning &a, const CompositionWarning &b) {
                    return a.code == b.code && a.element_id == b.element_id &&
                           a.message == b.message;
                  }),
      warnings.end());
}

} // namespace

std::expected<CompositionSolution, diagnostics::Error>
solve_composition(std::span<const LayoutElement> elements,
                  std::span<const CompositionConstraint> constraints,
                  double canvas_width_px, double canvas_height_px,
                  SolverLimits limits) {
  if (!(canvas_width_px > 0.0) || !(canvas_height_px > 0.0) ||
      !std::isfinite(canvas_width_px) || !std::isfinite(canvas_height_px)) {
    return std::unexpected(make_error(
        "invalid_canvas", "Canvas dimensions must be finite and > 0"));
  }
  if (limits.max_iterations == 0) {
    return std::unexpected(
        make_error("invalid_solver_limits", "max_iterations must be > 0"));
  }

  CompositionSolution solution;
  solution.elements.assign(elements.begin(), elements.end());
  sort_elements(solution.elements);

  std::unordered_map<std::string, std::size_t> index;
  for (std::size_t i = 0; i < solution.elements.size(); ++i) {
    if (solution.elements[i].id.empty()) {
      return std::unexpected(
          make_error("invalid_layout_element", "Layout element id is empty"));
    }
    if (!index.emplace(solution.elements[i].id, i).second) {
      return std::unexpected(make_error("duplicate_layout_element",
                                        "Duplicate layout element id"));
    }
    if (!std::isfinite(solution.elements[i].x_px) ||
        !std::isfinite(solution.elements[i].y_px) ||
        !std::isfinite(solution.elements[i].width_px) ||
        !std::isfinite(solution.elements[i].height_px) ||
        solution.elements[i].width_px < 0.0 ||
        solution.elements[i].height_px < 0.0) {
      return std::unexpected(make_error("invalid_layout_element",
                                        "Layout element geometry is invalid"));
    }
  }

  for (const CompositionConstraint &constraint : constraints) {
    if (constraint.element_id.empty() ||
        !index.contains(constraint.element_id)) {
      return std::unexpected(
          make_error("unknown_constraint_target",
                     "Composition constraint references an unknown element"));
    }
    if (constraint.other_id.has_value() &&
        !index.contains(*constraint.other_id)) {
      return std::unexpected(make_error(
          "unknown_constraint_target",
          "Composition constraint references an unknown other element"));
    }
  }

  // Stable constraint order: by kind id, then element id, then other id.
  std::vector<CompositionConstraint> ordered(constraints.begin(),
                                             constraints.end());
  std::sort(ordered.begin(), ordered.end(),
            [](const CompositionConstraint &a, const CompositionConstraint &b) {
              const auto ka = composition_constraint_id(a.kind);
              const auto kb = composition_constraint_id(b.kind);
              if (ka != kb) {
                return ka < kb;
              }
              if (a.element_id != b.element_id) {
                return a.element_id < b.element_id;
              }
              return a.other_id.value_or("") < b.other_id.value_or("");
            });

  for (std::uint32_t iter = 0; iter < limits.max_iterations; ++iter) {
    bool progressed = false;
    solution.warnings.clear();

    for (const CompositionConstraint &constraint : ordered) {
      LayoutElement *el =
          find_mutable(solution.elements, constraint.element_id);
      if (el == nullptr) {
        continue;
      }

      switch (constraint.kind) {
      case CompositionConstraintKind::bound_inside:
      case CompositionConstraintKind::keep_visible: {
        if (el->width_px > canvas_width_px ||
            el->height_px > canvas_height_px) {
          if (constraint.severity == ConstraintSeverity::hard) {
            return std::unexpected(make_error(
                "unsatisfiable_hard_constraint",
                "Element is larger than the canvas and cannot stay inside"));
          }
          solution.warnings.push_back({.code = "soft_bound_overflow",
                                       .message = "Element exceeds canvas size",
                                       .element_id = el->id});
          break;
        }
        if (!inside(*el, canvas_width_px, canvas_height_px)) {
          if (constraint.severity == ConstraintSeverity::hard ||
              el->keep_visible ||
              constraint.kind == CompositionConstraintKind::keep_visible) {
            const double before_x = el->x_px;
            const double before_y = el->y_px;
            clamp_inside(*el, canvas_width_px, canvas_height_px);
            progressed =
                progressed || before_x != el->x_px || before_y != el->y_px;
          } else {
            solution.warnings.push_back(
                {.code = "soft_out_of_bounds",
                 .message = "Element extends outside the canvas",
                 .element_id = el->id});
          }
        }
        break;
      }
      case CompositionConstraintKind::hierarchy_containment: {
        const std::string parent_id =
            constraint.other_id.value_or(el->parent_id.value_or(""));
        if (parent_id.empty()) {
          return std::unexpected(
              make_error("invalid_hierarchy_constraint",
                         "Hierarchy constraint requires a parent id"));
        }
        LayoutElement *parent = find_mutable(solution.elements, parent_id);
        if (parent == nullptr) {
          return std::unexpected(make_error("unknown_constraint_target",
                                            "Hierarchy parent is missing"));
        }
        if (el->width_px > parent->width_px ||
            el->height_px > parent->height_px) {
          if (constraint.severity == ConstraintSeverity::hard) {
            return std::unexpected(make_error(
                "unsatisfiable_hard_constraint",
                "Child is larger than its parent and cannot be contained"));
          }
          solution.warnings.push_back({.code = "soft_hierarchy_overflow",
                                       .message = "Child exceeds parent bounds",
                                       .element_id = el->id});
          break;
        }
        if (!contained(*el, *parent)) {
          if (constraint.severity == ConstraintSeverity::hard) {
            const double before_x = el->x_px;
            const double before_y = el->y_px;
            contain_in_parent(*el, *parent);
            progressed =
                progressed || before_x != el->x_px || before_y != el->y_px;
          } else {
            solution.warnings.push_back(
                {.code = "soft_hierarchy_breach",
                 .message = "Child is outside its parent",
                 .element_id = el->id});
          }
        }
        break;
      }
      case CompositionConstraintKind::text_legibility: {
        if (!el->text.has_value()) {
          return std::unexpected(make_error(
              "missing_text_metrics",
              "Text legibility constraint requires injected text metrics"));
        }
        // Prefer measured height; fall back to layout box height.
        const double text_h =
            el->text->height_px > 0.0 ? el->text->height_px : el->height_px;
        if (text_h + 1e-9 < constraint.min_text_height_px) {
          if (constraint.severity == ConstraintSeverity::hard) {
            return std::unexpected(make_error(
                "unsatisfiable_hard_constraint",
                "Text metrics fall below the minimum legibility height"));
          }
          solution.warnings.push_back(
              {.code = "soft_text_illegible",
               .message = "Text height is below the soft legibility threshold",
               .element_id = el->id});
        }
        break;
      }
      case CompositionConstraintKind::importance_overlap: {
        if (!constraint.other_id.has_value()) {
          return std::unexpected(
              make_error("invalid_overlap_constraint",
                         "Overlap constraint requires other_id"));
        }
        LayoutElement *other =
            find_mutable(solution.elements, *constraint.other_id);
        if (other == nullptr) {
          break;
        }
        if (!overlaps(rect_of(*el), rect_of(*other))) {
          break;
        }
        LayoutElement *low = el;
        LayoutElement *high = other;
        if (other->importance < el->importance ||
            (other->importance == el->importance && other->id < el->id)) {
          low = other;
          high = el;
        }
        if (constraint.severity == ConstraintSeverity::hard) {
          const double before_x = low->x_px;
          const double before_y = low->y_px;
          push_apart(*low, *high);
          clamp_inside(*low, canvas_width_px, canvas_height_px);
          if (!overlaps(rect_of(*low), rect_of(*high))) {
            progressed =
                progressed || before_x != low->x_px || before_y != low->y_px;
          } else {
            return std::unexpected(make_error(
                "unsatisfiable_hard_constraint",
                "Overlapping elements cannot be separated within the canvas"));
          }
        } else {
          solution.warnings.push_back(
              {.code = "soft_overlap",
               .message = "Elements overlap; lower importance was not moved",
               .element_id = low->id});
        }
        break;
      }
      }
    }

    if (!progressed) {
      break;
    }
  }

  // Final hard validation pass.
  for (const CompositionConstraint &constraint : ordered) {
    const LayoutElement *el =
        find_const(solution.elements, constraint.element_id);
    if (el == nullptr || constraint.severity != ConstraintSeverity::hard) {
      continue;
    }
    switch (constraint.kind) {
    case CompositionConstraintKind::bound_inside:
    case CompositionConstraintKind::keep_visible:
      if (!inside(*el, canvas_width_px, canvas_height_px)) {
        return std::unexpected(
            make_error("unsatisfiable_hard_constraint",
                       "Hard bound/keep-visible constraint remains violated"));
      }
      break;
    case CompositionConstraintKind::hierarchy_containment: {
      const std::string parent_id =
          constraint.other_id.value_or(el->parent_id.value_or(""));
      const LayoutElement *parent = find_const(solution.elements, parent_id);
      if (parent == nullptr || !contained(*el, *parent)) {
        return std::unexpected(
            make_error("unsatisfiable_hard_constraint",
                       "Hard hierarchy containment remains violated"));
      }
      break;
    }
    case CompositionConstraintKind::text_legibility: {
      const double text_h = el->text.has_value() && el->text->height_px > 0.0
                                ? el->text->height_px
                                : el->height_px;
      if (text_h + 1e-9 < constraint.min_text_height_px) {
        return std::unexpected(
            make_error("unsatisfiable_hard_constraint",
                       "Hard text legibility constraint remains violated"));
      }
      break;
    }
    case CompositionConstraintKind::importance_overlap: {
      const LayoutElement *other =
          find_const(solution.elements, constraint.other_id.value_or(""));
      if (other != nullptr && overlaps(rect_of(*el), rect_of(*other))) {
        return std::unexpected(
            make_error("unsatisfiable_hard_constraint",
                       "Hard overlap constraint remains violated"));
      }
      break;
    }
    }
  }

  sort_elements(solution.elements);
  sort_warnings(solution.warnings);
  return solution;
}

} // namespace instantee::direction
