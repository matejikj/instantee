#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Value metrics produced by the typography adapter (or a test double). The
// composition solver never links HarfBuzz/FreeType; callers inject measured px.
struct TextMetrics {
  double width_px = 0;
  double height_px = 0;
  double ascent_px = 0;
  double descent_px = 0;
};

struct LayoutElement {
  std::string id;
  std::optional<std::string> parent_id;
  double x_px = 0;
  double y_px = 0;
  double width_px = 0;
  double height_px = 0;
  // Element significance in [0, 1]; higher importance wins soft overlap nudges.
  double importance = 0.5;
  bool keep_visible = false;
  std::optional<TextMetrics> text{};
};

enum class ConstraintSeverity : std::uint8_t { soft = 1, hard };

enum class CompositionConstraintKind : std::uint8_t {
  bound_inside = 1,
  keep_visible,
  hierarchy_containment,
  text_legibility,
  importance_overlap,
};

struct CompositionConstraint {
  CompositionConstraintKind kind = CompositionConstraintKind::bound_inside;
  ConstraintSeverity severity = ConstraintSeverity::hard;
  std::string element_id;
  std::optional<std::string> other_id{};
  // For text_legibility: minimum rendered text height in px.
  double min_text_height_px = 24.0;
};

struct CompositionWarning {
  std::string code;
  std::string message;
  std::string element_id;
};

struct CompositionSolution {
  std::vector<LayoutElement> elements;
  std::vector<CompositionWarning> warnings;
};

struct SolverLimits {
  std::uint32_t max_iterations = 64;
};

// Deterministic bounded solver. Hard violations that remain after the iteration
// budget fail with diagnostics; soft conflicts become warnings. Identical
// inputs always yield identical element positions and warning order.
[[nodiscard]] std::expected<CompositionSolution, diagnostics::Error>
solve_composition(std::span<const LayoutElement> elements,
                  std::span<const CompositionConstraint> constraints,
                  double canvas_width_px, double canvas_height_px,
                  SolverLimits limits = {});

[[nodiscard]] constexpr std::string_view
composition_constraint_id(CompositionConstraintKind kind) noexcept {
  switch (kind) {
  case CompositionConstraintKind::bound_inside:
    return "bound_inside";
  case CompositionConstraintKind::keep_visible:
    return "keep_visible";
  case CompositionConstraintKind::hierarchy_containment:
    return "hierarchy_containment";
  case CompositionConstraintKind::text_legibility:
    return "text_legibility";
  case CompositionConstraintKind::importance_overlap:
    return "importance_overlap";
  }
  return "unknown";
}

} // namespace instantee::direction
