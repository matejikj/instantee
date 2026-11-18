#include <instantee/math/layout.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace instantee::math {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code, "math",
                            message};
}

[[nodiscard]] bool finite_nonneg(double v) noexcept {
  return std::isfinite(v) && v >= 0.0;
}

[[nodiscard]] std::size_t utf8_codepoint_count(std::string_view text) noexcept {
  std::size_t n = 0;
  for (unsigned char ch : text) {
    if ((ch & 0xC0U) != 0x80U) {
      ++n;
    }
  }
  return n;
}

[[nodiscard]] MathLayoutBox leaf_box(MathNodeKind kind, std::string tag,
                                     const MathAtomMetrics &m) {
  return MathLayoutBox{.width_px = m.width_px,
                       .height_px = m.height_px,
                       .depth_px = m.depth_px,
                       .source_kind = kind,
                       .tag = std::move(tag)};
}

struct Layouter {
  const MathMetricsProvider &metrics;
  MathLayoutStyle style;

  [[nodiscard]] std::expected<MathAtomMetrics, diagnostics::Error>
  measure_text(std::string_view text, float size_px) const {
    if (!(std::isfinite(size_px) && size_px > 0.0f)) {
      return std::unexpected(
          make_invalid("math.layout.invalid_size", "Math size_px must be > 0"));
    }
    auto measured = metrics.measure(text, size_px);
    if (!measured) {
      return std::unexpected(measured.error());
    }
    if (!finite_nonneg(measured->width_px) ||
        !finite_nonneg(measured->height_px) ||
        !finite_nonneg(measured->depth_px)) {
      return std::unexpected(make_invalid(
          "math.layout.invalid_metrics",
          "Metrics provider returned non-finite or negative values"));
    }
    return measured;
  }

  [[nodiscard]] std::expected<MathLayoutBox, diagnostics::Error>
  layout_node(const MathNode &node, float size_px) const;

  [[nodiscard]] std::expected<MathLayoutBox, diagnostics::Error>
  layout_row_children(const MathNode &node, float size_px,
                      std::string tag) const {
    MathLayoutBox row{.source_kind = node.kind, .tag = std::move(tag)};
    double cursor = 0;
    double height = 0;
    double depth = 0;
    for (const MathNode &child : node.children) {
      auto box = layout_node(child, size_px);
      if (!box) {
        return std::unexpected(box.error());
      }
      box->x_px = cursor;
      box->shift_px = 0;
      cursor += box->width_px;
      height = std::max(height, box->height_px);
      depth = std::max(depth, box->depth_px);
      row.children.push_back(std::move(*box));
    }
    row.width_px = cursor;
    row.height_px = height;
    row.depth_px = depth;
    return row;
  }
};

std::expected<MathLayoutBox, diagnostics::Error>
Layouter::layout_node(const MathNode &node, float size_px) const {
  switch (node.kind) {
  case MathNodeKind::row:
    return layout_row_children(node, size_px, "row");
  case MathNodeKind::group: {
    if (node.children.size() != 1) {
      return std::unexpected(make_invalid("math.layout.invalid_group",
                                          "Group node must have one child"));
    }
    auto inner = layout_node(node.children.front(), size_px);
    if (!inner) {
      return inner;
    }
    // Keep the child nested so AST and layout trees stay isomorphic for
    // semantic id assignment.
    MathLayoutBox group{.width_px = inner->width_px,
                        .height_px = inner->height_px,
                        .depth_px = inner->depth_px,
                        .source_kind = MathNodeKind::group,
                        .tag = "group"};
    group.children.push_back(std::move(*inner));
    return group;
  }
  case MathNodeKind::atom:
  case MathNodeKind::number:
  case MathNodeKind::symbol:
  case MathNodeKind::literal: {
    const std::string text = math_leaf_text(node);
    auto m = measure_text(text, size_px);
    if (!m) {
      return std::unexpected(m.error());
    }
    std::string tag = node.kind == MathNodeKind::symbol    ? "symbol"
                      : node.kind == MathNodeKind::number  ? "number"
                      : node.kind == MathNodeKind::literal ? "literal"
                                                           : "atom";
    if (!node.value.empty()) {
      tag.push_back(':');
      tag.append(node.value);
    }
    return leaf_box(node.kind, std::move(tag), *m);
  }
  case MathNodeKind::space: {
    double factor = 0.3;
    if (node.value == "quad") {
      factor = 1.0;
    } else if (node.value == "qquad") {
      factor = 2.0;
    } else if (node.value == ",") {
      factor = 0.1667;
    } else if (node.value == ":" || node.value == ";") {
      factor = 0.2222;
    } else if (node.value == "!") {
      factor = -0.1667;
    }
    const double width = static_cast<double>(size_px) * factor;
    MathLayoutBox box{.width_px = std::max(0.0, width),
                      .source_kind = MathNodeKind::space,
                      .tag = "space:" + node.value};
    return box;
  }
  case MathNodeKind::accent:
  case MathNodeKind::style: {
    if (node.children.size() != 1) {
      return std::unexpected(
          make_invalid("math.layout.invalid_wrapper",
                       "Accent/style node must wrap one child"));
    }
    auto body = layout_node(node.children.front(), size_px);
    if (!body) {
      return body;
    }
    MathLayoutBox box{
        .width_px = body->width_px,
        .height_px = body->height_px,
        .depth_px = body->depth_px,
        .source_kind = node.kind,
        .tag = (node.kind == MathNodeKind::accent ? "accent:" : "style:") +
               node.value};
    if (node.kind == MathNodeKind::accent) {
      // Reserve a thin accent band above the body without changing width.
      const double band = static_cast<double>(size_px) * 0.15;
      box.height_px += band;
    }
    box.children.push_back(std::move(*body));
    return box;
  }
  case MathNodeKind::delimited: {
    if (node.children.size() != 1) {
      return std::unexpected(
          make_invalid("math.layout.invalid_delimited",
                       "Delimited node must wrap one child"));
    }
    auto body = layout_node(node.children.front(), size_px);
    if (!body) {
      return body;
    }
    const auto delim = measure_text("(", size_px);
    if (!delim) {
      return std::unexpected(delim.error());
    }
    MathLayoutBox open = leaf_box(MathNodeKind::literal, "open", *delim);
    MathLayoutBox close = leaf_box(MathNodeKind::literal, "close", *delim);
    const double h =
        std::max({open.height_px, body->height_px, close.height_px});
    const double d = std::max({open.depth_px, body->depth_px, close.depth_px});
    open.height_px = close.height_px = h;
    open.depth_px = close.depth_px = d;
    body->x_px = open.width_px;
    close.x_px = open.width_px + body->width_px;
    MathLayoutBox box{.width_px =
                          open.width_px + body->width_px + close.width_px,
                      .height_px = h,
                      .depth_px = d,
                      .source_kind = MathNodeKind::delimited,
                      .tag = "delimited"};
    box.children.push_back(std::move(open));
    box.children.push_back(std::move(*body));
    box.children.push_back(std::move(close));
    return box;
  }
  case MathNodeKind::frac: {
    if (node.children.size() != 2) {
      return std::unexpected(
          make_invalid("math.layout.invalid_frac",
                       "Frac requires numerator and denominator"));
    }
    auto num = layout_node(node.children[0], size_px);
    if (!num) {
      return std::unexpected(num.error());
    }
    auto den = layout_node(node.children[1], size_px);
    if (!den) {
      return std::unexpected(den.error());
    }
    const double pad = static_cast<double>(size_px) * style.frac_pad_factor;
    const double gap = static_cast<double>(size_px) * style.frac_gap_factor;
    const double rule = static_cast<double>(size_px) * style.frac_rule_factor;
    const double axis = static_cast<double>(size_px) * style.axis_factor;
    const double inner_w = std::max(num->width_px, den->width_px);
    const double width = inner_w + 2.0 * pad;

    num->tag = "num";
    num->x_px = pad + 0.5 * (inner_w - num->width_px);
    // Raise numerator so its depth clears the axis + half rule + gap.
    num->shift_px = axis + 0.5 * rule + gap + num->depth_px;

    den->tag = "den";
    den->x_px = pad + 0.5 * (inner_w - den->width_px);
    // Baseline so the top of the denominator clears the rule + gap.
    den->shift_px = axis - 0.5 * rule - gap - den->height_px;

    MathLayoutBox rule_box{.width_px = width,
                           .height_px = 0.5 * rule,
                           .depth_px = 0.5 * rule,
                           .x_px = 0,
                           .shift_px = axis,
                           .source_kind = MathNodeKind::literal,
                           .tag = "rule"};

    MathLayoutBox box{.width_px = width,
                      .height_px = num->shift_px + num->height_px,
                      .depth_px = -den->shift_px + den->depth_px,
                      .source_kind = MathNodeKind::frac,
                      .tag = "frac"};
    box.children.push_back(std::move(*num));
    box.children.push_back(std::move(rule_box));
    box.children.push_back(std::move(*den));
    return box;
  }
  case MathNodeKind::sqrt: {
    if (node.children.empty() || node.children.size() > 2) {
      return std::unexpected(make_invalid("math.layout.invalid_sqrt",
                                          "Sqrt expects body or index+body"));
    }
    const bool has_index = node.children.size() == 2;
    const MathNode &body_node = has_index ? node.children[1] : node.children[0];
    auto body = layout_node(body_node, size_px);
    if (!body) {
      return std::unexpected(body.error());
    }
    body->tag = "radicand";

    const double gap = static_cast<double>(size_px) * style.sqrt_gap_factor;
    const double rule = static_cast<double>(size_px) * style.sqrt_rule_factor;
    const double surd_w =
        static_cast<double>(size_px) * style.sqrt_surd_width_factor;
    const double content_h = body->height_px + body->depth_px;
    const double inner_h = content_h + 2.0 * gap;

    MathLayoutBox surd{.width_px = surd_w,
                       .height_px = body->height_px + gap + rule,
                       .depth_px = body->depth_px + gap,
                       .source_kind = MathNodeKind::literal,
                       .tag = "surd"};
    MathLayoutBox overline{.width_px = body->width_px,
                           .height_px = rule,
                           .depth_px = 0,
                           .x_px = surd_w,
                           .shift_px = body->height_px + gap + 0.5 * rule,
                           .source_kind = MathNodeKind::literal,
                           .tag = "overline"};
    body->x_px = surd_w;
    body->shift_px = 0;

    MathLayoutBox box{
        .width_px = surd_w + body->width_px,
        .height_px =
            std::max(surd.height_px, overline.shift_px + overline.height_px),
        .depth_px = std::max(surd.depth_px, body->depth_px),
        .source_kind = MathNodeKind::sqrt,
        .tag = "sqrt"};
    (void)inner_h;

    if (has_index) {
      const float index_size = size_px * style.index_scale;
      auto index = layout_node(node.children[0], index_size);
      if (!index) {
        return std::unexpected(index.error());
      }
      index->tag = "index";
      index->x_px = static_cast<double>(size_px) * style.index_hspace_factor;
      index->shift_px = static_cast<double>(size_px) * style.index_raise_factor;
      box.width_px = std::max(box.width_px, index->x_px + index->width_px);
      box.height_px =
          std::max(box.height_px, index->shift_px + index->height_px);
      box.children.push_back(std::move(*index));
    }
    box.children.push_back(std::move(surd));
    box.children.push_back(std::move(overline));
    box.children.push_back(std::move(*body));
    return box;
  }
  case MathNodeKind::script: {
    if (node.children.size() != 3) {
      return std::unexpected(make_invalid("math.layout.invalid_script",
                                          "Script node must be base/sub/sup"));
    }
    auto base = layout_node(node.children[0], size_px);
    if (!base) {
      return std::unexpected(base.error());
    }
    base->tag = "base";
    const float script_size = size_px * style.script_scale;
    const double space =
        static_cast<double>(size_px) * style.script_space_factor;
    const bool has_sub = !is_empty_row(node.children[1]);
    const bool has_sup = !is_empty_row(node.children[2]);

    MathLayoutBox sub{};
    MathLayoutBox sup{};
    if (has_sub) {
      auto laid = layout_node(node.children[1], script_size);
      if (!laid) {
        return std::unexpected(laid.error());
      }
      sub = std::move(*laid);
      sub.tag = "sub";
      sub.x_px = base->width_px + space;
      sub.shift_px =
          -(0.2 * static_cast<double>(size_px) + sub.height_px * 0.1);
    }
    if (has_sup) {
      auto laid = layout_node(node.children[2], script_size);
      if (!laid) {
        return std::unexpected(laid.error());
      }
      sup = std::move(*laid);
      sup.tag = "sup";
      sup.x_px = base->width_px + space;
      sup.shift_px = 0.45 * static_cast<double>(size_px);
    }

    MathLayoutBox box{.source_kind = MathNodeKind::script, .tag = "script"};
    box.width_px = base->width_px;
    box.height_px = base->height_px;
    box.depth_px = base->depth_px;
    box.children.push_back(std::move(*base));
    if (has_sub) {
      box.width_px = std::max(box.width_px, sub.x_px + sub.width_px);
      box.depth_px = std::max(box.depth_px, -sub.shift_px + sub.depth_px);
      box.children.push_back(std::move(sub));
    }
    if (has_sup) {
      box.width_px = std::max(box.width_px, sup.x_px + sup.width_px);
      box.height_px = std::max(box.height_px, sup.shift_px + sup.height_px);
      box.children.push_back(std::move(sup));
    }
    return box;
  }
  }
  return std::unexpected(
      make_invalid("math.layout.unknown_kind", "Unknown math node kind"));
}

void dump_box(std::ostringstream &out, const MathLayoutBox &box, int indent) {
  out << std::string(static_cast<std::size_t>(indent), ' ') << box.tag
      << " kind=" << static_cast<int>(box.source_kind) << std::fixed
      << std::setprecision(3) << " w=" << box.width_px << " h=" << box.height_px
      << " d=" << box.depth_px << " x=" << box.x_px << " shift=" << box.shift_px
      << '\n';
  for (const MathLayoutBox &child : box.children) {
    dump_box(out, child, indent + 2);
  }
}

} // namespace

std::expected<MathAtomMetrics, diagnostics::Error>
UnitMathMetrics::measure(std::string_view text, float size_px) const {
  if (!(std::isfinite(size_px) && size_px > 0.0f)) {
    return std::unexpected(
        make_invalid("math.layout.invalid_size", "Math size_px must be > 0"));
  }
  const std::size_t cps = utf8_codepoint_count(text);
  const double size = static_cast<double>(size_px);
  return MathAtomMetrics{.width_px = 0.5 * size * static_cast<double>(cps),
                         .height_px = 0.8 * size,
                         .depth_px = 0.2 * size};
}

std::string math_leaf_text(const MathNode &node) {
  if (node.kind == MathNodeKind::symbol) {
    // Stable ASCII stand-ins keep unit-metric goldens independent of Unicode.
    if (node.value == "alpha")
      return "a";
    if (node.value == "beta")
      return "b";
    if (node.value == "gamma")
      return "g";
    if (node.value == "theta")
      return "q";
    if (node.value == "infty")
      return "8";
    if (node.value == "cdot" || node.value == "times")
      return "*";
    if (node.value == "pm" || node.value == "mp")
      return "+";
    if (node.value == "leq" || node.value == "geq" || node.value == "neq" ||
        node.value == "approx" || node.value == "equiv" || node.value == "sim")
      return "=";
    if (node.value == "to" || node.value == "rightarrow" ||
        node.value == "leftarrow" || node.value == "Rightarrow")
      return ">";
    if (node.value == "sin" || node.value == "cos" || node.value == "tan" ||
        node.value == "log" || node.value == "ln" || node.value == "exp" ||
        node.value == "lim" || node.value == "max" || node.value == "min" ||
        node.value == "det")
      return node.value.substr(0, std::min<std::size_t>(node.value.size(), 3));
    if (!node.value.empty())
      return std::string(1, node.value.front());
    return "?";
  }
  if (node.kind == MathNodeKind::literal && node.value.size() == 1) {
    return node.value;
  }
  return node.value;
}

std::expected<MathLayoutBox, diagnostics::Error>
layout_math(const MathNode &root, const MathMetricsProvider &metrics,
            const MathLayoutStyle &style) {
  if (!(std::isfinite(style.size_px) && style.size_px > 0.0f) ||
      !(std::isfinite(style.script_scale) && style.script_scale > 0.0f &&
        style.script_scale <= 1.0f)) {
    return std::unexpected(make_invalid("math.layout.invalid_style",
                                        "Math layout style sizes are invalid"));
  }
  Layouter layouter{.metrics = metrics, .style = style};
  return layouter.layout_node(root, style.size_px);
}

std::string format_math_layout_golden(const MathLayoutBox &box) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(3);
  dump_box(out, box, 0);
  return out.str();
}

} // namespace instantee::math
