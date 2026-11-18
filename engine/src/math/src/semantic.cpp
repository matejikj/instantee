#include <instantee/math/semantic.hpp>

#include <utility>

namespace instantee::math {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code, "math",
                            message};
}

[[nodiscard]] std::string join_path(std::string_view parent,
                                    std::string_view segment) {
  std::string out;
  out.reserve(parent.size() + segment.size() + 1);
  out.append(parent);
  if (!parent.empty() && parent.back() != '/') {
    out.push_back('/');
  }
  out.append(segment);
  return out;
}

[[nodiscard]] bool is_decorative_tag(std::string_view tag) noexcept {
  return tag == "rule" || tag == "surd" || tag == "overline" || tag == "open" ||
         tag == "close";
}

[[nodiscard]] bool is_slot_role(std::string_view role) noexcept {
  return role == "num" || role == "den" || role == "base" || role == "sub" ||
         role == "sup" || role == "index" || role == "radicand" ||
         role == "body";
}

[[nodiscard]] bool is_leaf_kind(MathNodeKind kind) noexcept {
  return kind == MathNodeKind::atom || kind == MathNodeKind::number ||
         kind == MathNodeKind::symbol || kind == MathNodeKind::literal ||
         kind == MathNodeKind::space;
}

[[nodiscard]] std::string leaf_segment(const MathNode &ast) {
  switch (ast.kind) {
  case MathNodeKind::atom:
    return "atom:" + ast.value;
  case MathNodeKind::number:
    return "num:" + ast.value;
  case MathNodeKind::symbol:
    return "sym:" + ast.value;
  case MathNodeKind::literal:
    return "lit:" + ast.value;
  case MathNodeKind::space:
    return "space:" + ast.value;
  default:
    return "leaf";
  }
}

[[nodiscard]] std::string kind_segment(const MathNode &ast) {
  switch (ast.kind) {
  case MathNodeKind::row:
    return "row";
  case MathNodeKind::group:
    return "group";
  case MathNodeKind::frac:
    return "frac";
  case MathNodeKind::sqrt:
    return "sqrt";
  case MathNodeKind::script:
    return "script";
  case MathNodeKind::accent:
    return "accent:" + ast.value;
  case MathNodeKind::style:
    return "style:" + ast.value;
  case MathNodeKind::delimited:
    return "delim";
  default:
    return leaf_segment(ast);
  }
}

[[nodiscard]] std::string make_node_id(std::string_view parent_path,
                                       std::string_view role,
                                       const MathNode &ast) {
  if (parent_path.empty()) {
    return "m";
  }
  if (is_slot_role(role)) {
    const std::string slotted = join_path(parent_path, role);
    // Group/row under a named slot owns the slot id (no extra /group).
    if (ast.kind == MathNodeKind::group || ast.kind == MathNodeKind::row) {
      return slotted;
    }
    if (is_leaf_kind(ast.kind)) {
      return join_path(slotted, leaf_segment(ast));
    }
    return join_path(slotted, kind_segment(ast));
  }
  // Row sibling index ("0","1",…) or other free roles.
  if (!role.empty() && role != "root" && role != "leaf") {
    const std::string indexed = join_path(parent_path, role);
    if (is_leaf_kind(ast.kind)) {
      return join_path(indexed, leaf_segment(ast));
    }
    return join_path(indexed, kind_segment(ast));
  }
  return join_path(parent_path, kind_segment(ast));
}

struct Compiler {
  MathAnimScene scene;

  void push_node(MathAnimNode node) { scene.nodes.push_back(std::move(node)); }

  void push_command(MathRenderCommand cmd) {
    scene.commands.push_back(std::move(cmd));
  }

  [[nodiscard]] std::expected<void, diagnostics::Error>
  emit_leaf_commands(const MathAnimNode &node) {
    if (node.glyph_text.empty() && node.kind != MathNodeKind::space) {
      return {};
    }
    if (node.kind == MathNodeKind::space) {
      return {};
    }
    push_command(MathRenderCommand{.op = MathRenderOp::text,
                                   .owner_id = node.id,
                                   .x_px = node.x_px,
                                   .baseline_y_px = node.baseline_y_px,
                                   .width_px = node.width_px,
                                   .height_px = node.height_px,
                                   .depth_px = node.depth_px,
                                   .text = node.glyph_text});
    return {};
  }

  [[nodiscard]] std::expected<void, diagnostics::Error>
  emit_decorative(const MathLayoutBox &box, std::string_view owner_id,
                  double abs_x, double abs_baseline) {
    MathRenderOp op = MathRenderOp::rule;
    if (box.tag == "surd") {
      op = MathRenderOp::radical;
    } else if (box.tag == "rule" || box.tag == "overline") {
      op = MathRenderOp::rule;
    } else if (box.tag == "open" || box.tag == "close") {
      op = MathRenderOp::text;
    } else {
      return {};
    }
    push_command(
        MathRenderCommand{.op = op,
                          .owner_id = std::string(owner_id),
                          .x_px = abs_x + box.x_px,
                          .baseline_y_px = abs_baseline + box.shift_px,
                          .width_px = box.width_px,
                          .height_px = box.height_px,
                          .depth_px = box.depth_px,
                          .text = (box.tag == "open"    ? "("
                                   : box.tag == "close" ? ")"
                                                        : std::string{})});
    return {};
  }

  // Walk AST and layout in lockstep. Decorative layout boxes are paint-only
  // under the current semantic owner; they never mint new semantic ids.
  [[nodiscard]] std::expected<std::string, diagnostics::Error>
  compile_node(const MathNode &ast, const MathLayoutBox &layout,
               std::string_view parent_path, std::string_view role,
               double abs_x, double abs_baseline) {
    const double node_x = abs_x + layout.x_px;
    const double node_baseline = abs_baseline + layout.shift_px;
    const std::string id = make_node_id(parent_path, role, ast);

    MathAnimNode node{.id = id,
                      .kind = ast.kind,
                      .role = std::string(role),
                      .glyph_text = {},
                      .x_px = node_x,
                      .baseline_y_px = node_baseline,
                      .width_px = layout.width_px,
                      .height_px = layout.height_px,
                      .depth_px = layout.depth_px};

    const bool is_leaf = is_leaf_kind(ast.kind);
    if (is_leaf) {
      node.glyph_text = math_leaf_text(ast);
      if (node.role.empty()) {
        node.role = "leaf";
      }
      auto painted = emit_leaf_commands(node);
      if (!painted) {
        return std::unexpected(painted.error());
      }
      push_node(std::move(node));
      return id;
    }

    // Structural nodes: correlate AST children with non-decorative layout kids.
    std::vector<const MathLayoutBox *> semantic_boxes;
    semantic_boxes.reserve(layout.children.size());
    for (const MathLayoutBox &child : layout.children) {
      if (is_decorative_tag(child.tag)) {
        auto deco = emit_decorative(child, id, node_x, node_baseline);
        if (!deco) {
          return std::unexpected(deco.error());
        }
        continue;
      }
      semantic_boxes.push_back(&child);
    }

    // Layout may wrap a single group/row without repeating AST shape (e.g.
    // group layout replaces itself with the inner row metrics but keeps one
    // child). Handle common alignments explicitly per kind.
    auto compile_child = [&](const MathNode &child_ast,
                             const MathLayoutBox &child_layout,
                             std::string_view child_role)
        -> std::expected<std::string, diagnostics::Error> {
      return compile_node(child_ast, child_layout, id, child_role, node_x,
                          node_baseline);
    };

    switch (ast.kind) {
    case MathNodeKind::row: {
      if (semantic_boxes.size() != ast.children.size()) {
        return std::unexpected(
            make_invalid("math.semantic.layout_mismatch",
                         "Row layout children do not match AST siblings"));
      }
      for (std::size_t i = 0; i < ast.children.size(); ++i) {
        auto child_id = compile_child(ast.children[i], *semantic_boxes[i],
                                      std::to_string(i));
        if (!child_id) {
          return std::unexpected(child_id.error());
        }
        node.child_ids.push_back(*child_id);
      }
      break;
    }
    case MathNodeKind::group:
    case MathNodeKind::accent:
    case MathNodeKind::style:
    case MathNodeKind::delimited: {
      // Delimited layout keeps open/close as decorative; body remains.
      if (ast.children.size() != 1 || semantic_boxes.size() != 1) {
        return std::unexpected(
            make_invalid("math.semantic.layout_mismatch",
                         "Wrapper node expects one semantic child"));
      }
      auto child_id = compile_child(ast.children[0], *semantic_boxes[0], "");
      if (!child_id) {
        return std::unexpected(child_id.error());
      }
      node.child_ids.push_back(*child_id);
      break;
    }
    case MathNodeKind::frac: {
      if (ast.children.size() != 2) {
        return std::unexpected(make_invalid("math.semantic.invalid_frac",
                                            "Frac AST needs two children"));
      }
      // semantic_boxes should be [num, den] after skipping rule.
      if (semantic_boxes.size() != 2) {
        return std::unexpected(
            make_invalid("math.semantic.layout_mismatch",
                         "Frac layout expects num and den boxes"));
      }
      auto num_id = compile_child(ast.children[0], *semantic_boxes[0], "num");
      if (!num_id) {
        return std::unexpected(num_id.error());
      }
      auto den_id = compile_child(ast.children[1], *semantic_boxes[1], "den");
      if (!den_id) {
        return std::unexpected(den_id.error());
      }
      node.child_ids.push_back(*num_id);
      node.child_ids.push_back(*den_id);
      break;
    }
    case MathNodeKind::sqrt: {
      const bool has_index = ast.children.size() == 2;
      if (ast.children.empty() || ast.children.size() > 2) {
        return std::unexpected(make_invalid("math.semantic.invalid_sqrt",
                                            "Sqrt AST shape is invalid"));
      }
      // After skipping surd/overline, remaining: optional index, radicand.
      if (semantic_boxes.size() != (has_index ? 2U : 1U)) {
        return std::unexpected(make_invalid("math.semantic.layout_mismatch",
                                            "Sqrt layout does not match AST"));
      }
      std::size_t box_i = 0;
      if (has_index) {
        auto index_id =
            compile_child(ast.children[0], *semantic_boxes[box_i++], "index");
        if (!index_id) {
          return std::unexpected(index_id.error());
        }
        node.child_ids.push_back(*index_id);
      }
      const MathNode &body_ast = has_index ? ast.children[1] : ast.children[0];
      auto body_id =
          compile_child(body_ast, *semantic_boxes[box_i], "radicand");
      if (!body_id) {
        return std::unexpected(body_id.error());
      }
      node.child_ids.push_back(*body_id);
      break;
    }
    case MathNodeKind::script: {
      if (ast.children.size() != 3 || semantic_boxes.empty()) {
        return std::unexpected(
            make_invalid("math.semantic.invalid_script",
                         "Script AST/layout shape is invalid"));
      }
      // Layout children: base, optional sub, optional sup (empty sides
      // omitted).
      std::size_t box_i = 0;
      auto base_id =
          compile_child(ast.children[0], *semantic_boxes[box_i++], "base");
      if (!base_id) {
        return std::unexpected(base_id.error());
      }
      node.child_ids.push_back(*base_id);
      if (!is_empty_row(ast.children[1])) {
        if (box_i >= semantic_boxes.size()) {
          return std::unexpected(make_invalid("math.semantic.layout_mismatch",
                                              "Missing subscript layout box"));
        }
        auto sub_id =
            compile_child(ast.children[1], *semantic_boxes[box_i++], "sub");
        if (!sub_id) {
          return std::unexpected(sub_id.error());
        }
        node.child_ids.push_back(*sub_id);
      }
      if (!is_empty_row(ast.children[2])) {
        if (box_i >= semantic_boxes.size()) {
          return std::unexpected(
              make_invalid("math.semantic.layout_mismatch",
                           "Missing superscript layout box"));
        }
        auto sup_id =
            compile_child(ast.children[2], *semantic_boxes[box_i++], "sup");
        if (!sup_id) {
          return std::unexpected(sup_id.error());
        }
        node.child_ids.push_back(*sup_id);
      }
      break;
    }
    default:
      return std::unexpected(make_invalid("math.semantic.unknown_kind",
                                          "Unsupported math node kind"));
    }

    push_node(std::move(node));
    return id;
  }
};

} // namespace

std::expected<MathAnimScene, diagnostics::Error>
compile_math_anim(const MathNode &ast, const MathLayoutBox &layout) {
  Compiler compiler;
  auto root = compiler.compile_node(ast, layout, "", "root", 0.0, 0.0);
  if (!root) {
    return std::unexpected(root.error());
  }
  compiler.scene.root_id = *root;
  return std::move(compiler.scene);
}

const MathAnimNode *find_math_anim_node(const MathAnimScene &scene,
                                        std::string_view id) noexcept {
  for (const MathAnimNode &node : scene.nodes) {
    if (node.id == id) {
      return &node;
    }
  }
  return nullptr;
}

std::expected<MathHighlightBounds, diagnostics::Error>
highlight_math_subexpression(const MathAnimScene &scene, std::string_view id) {
  const MathAnimNode *node = find_math_anim_node(scene, id);
  if (node == nullptr) {
    return std::unexpected(
        make_invalid("math.semantic.unknown_id",
                     "No math subexpression with the requested semantic id"));
  }
  return MathHighlightBounds{.id = node->id,
                             .x_px = node->x_px,
                             .baseline_y_px = node->baseline_y_px,
                             .width_px = node->width_px,
                             .height_px = node->height_px,
                             .depth_px = node->depth_px};
}

} // namespace instantee::math
