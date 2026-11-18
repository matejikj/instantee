#include <instantee/math/layout.hpp>
#include <instantee/math/parse.hpp>
#include <instantee/math/semantic.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>

namespace {

using instantee::math::compile_math_anim;
using instantee::math::find_math_anim_node;
using instantee::math::highlight_math_subexpression;
using instantee::math::layout_math;
using instantee::math::MathLayoutStyle;
using instantee::math::parse_math;
using instantee::math::UnitMathMetrics;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  UnitMathMetrics metrics;
  MathLayoutStyle style;
  style.size_px = 10.0f;

  auto ast = parse_math("\\frac{a+b}{\\sqrt{c}}");
  expect(ast.has_value(), "parse");
  if (!ast) {
    return EXIT_FAILURE;
  }
  auto layout = layout_math(*ast, metrics, style);
  expect(layout.has_value(), "layout");
  if (!layout) {
    return EXIT_FAILURE;
  }
  auto scene = compile_math_anim(*ast, *layout);
  expect(scene.has_value(), "compile anim");
  if (!scene) {
    std::cerr << "  " << scene.error().code << ": " << scene.error().message
              << '\n';
    return EXIT_FAILURE;
  }

  expect(!scene->root_id.empty(), "root id");
  expect(!scene->nodes.empty(), "nodes");
  expect(!scene->commands.empty(), "render commands");

  // IDs are unique and path-stable (no pixel coordinates in the id string).
  std::unordered_set<std::string> ids;
  for (const auto &node : scene->nodes) {
    expect(ids.insert(node.id).second, "unique ids");
    expect(node.id.find("x=") == std::string::npos, "id has no geometry");
    expect(node.id.find("w=") == std::string::npos, "id has no size");
  }

  // Glyph payload is separate from semantic identity.
  const auto *atom_a = find_math_anim_node(*scene, "m/0/frac/num/row/0/atom:a");
  // Path may vary with row wrapping — search by suffix.
  const instantee::math::MathAnimNode *found_a = atom_a;
  if (found_a == nullptr) {
    for (const auto &node : scene->nodes) {
      if (node.kind == instantee::math::MathNodeKind::atom &&
          node.glyph_text == "a") {
        found_a = &node;
        break;
      }
    }
  }
  expect(found_a != nullptr, "find atom a");
  if (found_a) {
    expect(found_a->glyph_text == "a", "glyph text");
    expect(found_a->id.find("atom:a") != std::string::npos, "semantic leaf id");
    expect(found_a->id != found_a->glyph_text, "id distinct from glyph");
  }

  // Highlight numerator by semantic id without pixel picking.
  const instantee::math::MathAnimNode *num = nullptr;
  for (const auto &node : scene->nodes) {
    if (node.role == "num") {
      num = &node;
      break;
    }
  }
  expect(num != nullptr, "numerator node");
  if (num) {
    auto hi = highlight_math_subexpression(*scene, num->id);
    expect(hi.has_value(), "highlight num");
    if (hi) {
      expect(hi->id == num->id, "highlight id");
      expect(hi->width_px > 0.0 && hi->height_px > 0.0, "highlight bounds");
      expect(hi->x_px == num->x_px && hi->baseline_y_px == num->baseline_y_px,
             "bounds from node, not pixels");
    }
  }

  // Unknown id fails closed.
  auto missing = highlight_math_subexpression(*scene, "m/does/not/exist");
  expect(!missing.has_value() &&
             missing.error().code == "math.semantic.unknown_id",
         "unknown id");

  // Stability: identical AST+layout → identical id set.
  auto scene2 = compile_math_anim(*ast, *layout);
  expect(scene2.has_value(), "recompile");
  if (scene2) {
    expect(scene2->nodes.size() == scene->nodes.size(), "same node count");
    for (std::size_t i = 0; i < scene->nodes.size(); ++i) {
      expect(scene->nodes[i].id == scene2->nodes[i].id, "stable ids");
    }
  }

  // Script subexpression addressable for highlight.
  {
    auto script_ast = parse_math("x^{2}_{i}");
    expect(script_ast.has_value(), "script parse");
    if (script_ast) {
      auto script_layout = layout_math(*script_ast, metrics, style);
      expect(script_layout.has_value(), "script layout");
      if (script_layout) {
        auto script_scene = compile_math_anim(*script_ast, *script_layout);
        expect(script_scene.has_value(), "script compile");
        if (script_scene) {
          const instantee::math::MathAnimNode *sup = nullptr;
          for (const auto &node : script_scene->nodes) {
            if (node.role == "sup") {
              sup = &node;
              break;
            }
          }
          expect(sup != nullptr, "superscript node");
          if (sup) {
            auto hi = highlight_math_subexpression(*script_scene, sup->id);
            expect(hi.has_value() && hi->height_px > 0.0, "highlight sup");
          }
        }
      }
    }
  }

  // Text commands carry glyph text; ornaments stay under owner ids.
  bool saw_text = false;
  bool saw_rule = false;
  for (const auto &cmd : scene->commands) {
    if (cmd.op == instantee::math::MathRenderOp::text && !cmd.text.empty()) {
      saw_text = true;
      expect(!cmd.owner_id.empty(), "text has owner");
    }
    if (cmd.op == instantee::math::MathRenderOp::rule) {
      saw_rule = true;
      expect(cmd.owner_id.find("frac") != std::string::npos ||
                 cmd.owner_id.find("sqrt") != std::string::npos,
             "rule owned by structure");
    }
  }
  expect(saw_text, "text commands");
  expect(saw_rule, "rule commands");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
