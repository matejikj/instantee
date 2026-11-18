#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/math/ast.hpp>
#include <instantee/math/layout.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::math {

// Platform-neutral draw ops for one math expression. Glyph text is attached to
// commands; semantic identity lives on MathAnimNode::id.
enum class MathRenderOp : std::uint8_t {
  text = 1,
  rule,
  radical,
};

struct MathRenderCommand {
  MathRenderOp op = MathRenderOp::text;
  // Semantic entity that owns this paint (highlight/morph target).
  std::string owner_id{};
  double x_px = 0;          // left edge in root baseline space
  double baseline_y_px = 0; // baseline; +y is up from the root baseline
  double width_px = 0;
  double height_px = 0;
  double depth_px = 0;
  std::string text{}; // glyph payload for text ops; empty for ornaments
};

// One animatable semantic entity. The id is derived from AST structure only
// (never from pixel bounds), so authors can address subexpressions by path.
struct MathAnimNode {
  std::string id{};
  MathNodeKind kind = MathNodeKind::row;
  std::string role{};       // num/den/base/sub/sup/radicand/index/leaf/…
  std::string glyph_text{}; // leaf payload; empty for structural nodes
  double x_px = 0;
  double baseline_y_px = 0;
  double width_px = 0;
  double height_px = 0;
  double depth_px = 0;
  std::vector<std::string> child_ids{};
};

struct MathAnimScene {
  std::string root_id{};
  std::vector<MathAnimNode> nodes{};
  std::vector<MathRenderCommand> commands{};
};

struct MathHighlightBounds {
  std::string id{};
  double x_px = 0;
  double baseline_y_px = 0;
  double width_px = 0;
  double height_px = 0;
  double depth_px = 0;
};

// Lower AST + layout into animatable nodes and paint commands. IDs are stable
// for a given AST shape; layout pixels never participate in id assignment.
[[nodiscard]] std::expected<MathAnimScene, diagnostics::Error>
compile_math_anim(const MathNode &ast, const MathLayoutBox &layout);

[[nodiscard]] const MathAnimNode *
find_math_anim_node(const MathAnimScene &scene, std::string_view id) noexcept;

// Resolve a subexpression highlight from a semantic id (no pixel picking).
[[nodiscard]] std::expected<MathHighlightBounds, diagnostics::Error>
highlight_math_subexpression(const MathAnimScene &scene, std::string_view id);

} // namespace instantee::math
