#include <instantee/math/morph.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace instantee::math {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code, "math",
                            message};
}

[[nodiscard]] double clamp01(double v) noexcept {
  if (!(v > 0.0)) {
    return 0.0;
  }
  if (v > 1.0) {
    return 1.0;
  }
  return v;
}

[[nodiscard]] double lerp(double a, double b, double t) noexcept {
  return a + (b - a) * t;
}

// Strip leaf payloads from semantic ids so `\frac{a}{b}` and `\frac{a}{c}`
// share the den slot key `…/atom` instead of diverging on `:b` vs `:c`.
[[nodiscard]] std::string structural_key(std::string_view id) {
  std::string out;
  out.reserve(id.size());
  for (std::size_t i = 0; i < id.size();) {
    if (id[i] == ':') {
      ++i;
      while (i < id.size() && id[i] != '/') {
        ++i;
      }
      continue;
    }
    out.push_back(id[i]);
    ++i;
  }
  return out;
}

[[nodiscard]] bool is_leaf_token(const MathAnimNode &node) noexcept {
  return !node.glyph_text.empty();
}

[[nodiscard]] const MathAnimNode *require_node(const MathAnimScene &scene,
                                               std::string_view id,
                                               diagnostics::Error *out_err) {
  const MathAnimNode *node = find_math_anim_node(scene, id);
  if (node == nullptr) {
    *out_err = err("math_morph_missing_id",
                   "Morph plan references an id absent from the scene");
  }
  return node;
}

} // namespace

double sample_math_morph_ease(MathMorphEase ease, double unit_time) noexcept {
  const double u = clamp01(unit_time);
  switch (ease) {
  case MathMorphEase::linear:
    return u;
  case MathMorphEase::ease_in_out:
    // Smoothstep Hermite: exact at 0/1, C1 flat endpoints.
    return u * u * (3.0 - 2.0 * u);
  }
  return u;
}

std::expected<MathMorphPlan, diagnostics::Error>
plan_math_morph(const MathAnimScene &from, const MathAnimScene &to) {
  if (from.root_id.empty() || to.root_id.empty()) {
    return std::unexpected(
        err("math_morph_empty_scene", "Morph scenes require a root_id"));
  }

  struct Candidate {
    std::size_t index = 0;
    const MathAnimNode *node = nullptr;
  };

  std::unordered_map<std::string, std::vector<Candidate>> from_by_key;
  from_by_key.reserve(from.nodes.size());
  for (std::size_t i = 0; i < from.nodes.size(); ++i) {
    const MathAnimNode &node = from.nodes[i];
    from_by_key[structural_key(node.id)].push_back(
        Candidate{.index = i, .node = &node});
  }

  MathMorphPlan plan;
  plan.pairs.reserve(from.nodes.size() + to.nodes.size());
  std::unordered_set<std::string> used_from;
  used_from.reserve(from.nodes.size());

  for (const MathAnimNode &to_node : to.nodes) {
    const std::string key = structural_key(to_node.id);
    auto it = from_by_key.find(key);
    const MathAnimNode *best = nullptr;
    if (it != from_by_key.end()) {
      // Prefer same glyph when several from-nodes share a structural key
      // (rare sibling collisions); otherwise take the first unused.
      for (const Candidate &c : it->second) {
        if (used_from.contains(c.node->id)) {
          continue;
        }
        if (c.node->kind != to_node.kind || c.node->role != to_node.role) {
          continue;
        }
        if (is_leaf_token(*c.node) != is_leaf_token(to_node)) {
          continue;
        }
        if (is_leaf_token(to_node) &&
            c.node->glyph_text == to_node.glyph_text) {
          best = c.node;
          break;
        }
        if (!is_leaf_token(to_node) && best == nullptr) {
          best = c.node;
        }
        if (is_leaf_token(to_node) && best == nullptr) {
          // Same slot, different glyph → will become exit+enter below.
          best = c.node;
        }
      }
    }

    if (best != nullptr && is_leaf_token(to_node) &&
        best->glyph_text != to_node.glyph_text) {
      // Different glyphs must not lerp into a broken token.
      plan.pairs.push_back(MathMorphPair{
          .role = MathMorphRole::exit,
          .correspondence_key = key + "#exit:" + best->id,
          .from_id = best->id,
          .to_id = {},
      });
      used_from.insert(best->id);
      plan.pairs.push_back(MathMorphPair{
          .role = MathMorphRole::enter,
          .correspondence_key = key + "#enter:" + to_node.id,
          .from_id = {},
          .to_id = to_node.id,
      });
      continue;
    }

    if (best != nullptr) {
      used_from.insert(best->id);
      plan.pairs.push_back(MathMorphPair{
          .role = MathMorphRole::persist,
          .correspondence_key = key,
          .from_id = best->id,
          .to_id = to_node.id,
      });
      continue;
    }

    plan.pairs.push_back(MathMorphPair{
        .role = MathMorphRole::enter,
        .correspondence_key = key + "#enter:" + to_node.id,
        .from_id = {},
        .to_id = to_node.id,
    });
  }

  for (const MathAnimNode &from_node : from.nodes) {
    if (used_from.contains(from_node.id)) {
      continue;
    }
    const std::string key = structural_key(from_node.id);
    plan.pairs.push_back(MathMorphPair{
        .role = MathMorphRole::exit,
        .correspondence_key = key + "#exit:" + from_node.id,
        .from_id = from_node.id,
        .to_id = {},
    });
  }

  std::sort(plan.pairs.begin(), plan.pairs.end(),
            [](const MathMorphPair &a, const MathMorphPair &b) {
              return a.correspondence_key < b.correspondence_key;
            });
  return plan;
}

std::expected<std::vector<MathMorphToken>, diagnostics::Error>
sample_math_morph(const MathMorphPlan &plan, const MathAnimScene &from,
                  const MathAnimScene &to, double unit_time,
                  MathMorphEase ease) {
  if (!std::isfinite(unit_time)) {
    return std::unexpected(
        err("math_morph_bad_time", "Morph unit_time must be finite"));
  }

  const double e = sample_math_morph_ease(ease, unit_time);
  std::vector<MathMorphToken> out;
  out.reserve(plan.pairs.size());
  diagnostics::Error lookup_err;

  for (const MathMorphPair &pair : plan.pairs) {
    switch (pair.role) {
    case MathMorphRole::persist: {
      const MathAnimNode *a = require_node(from, pair.from_id, &lookup_err);
      if (a == nullptr) {
        return std::unexpected(lookup_err);
      }
      const MathAnimNode *b = require_node(to, pair.to_id, &lookup_err);
      if (b == nullptr) {
        return std::unexpected(lookup_err);
      }
      if (!is_leaf_token(*a)) {
        break; // structure drives matching only
      }
      if (a->glyph_text != b->glyph_text) {
        return std::unexpected(
            err("math_morph_broken_persist",
                "Persist pairs must share identical glyph text"));
      }
      out.push_back(MathMorphToken{
          .role = MathMorphRole::persist,
          .correspondence_key = pair.correspondence_key,
          .owner_id = pair.to_id,
          .glyph_text = a->glyph_text,
          .x_px = lerp(a->x_px, b->x_px, e),
          .baseline_y_px = lerp(a->baseline_y_px, b->baseline_y_px, e),
          .width_px = lerp(a->width_px, b->width_px, e),
          .height_px = lerp(a->height_px, b->height_px, e),
          .depth_px = lerp(a->depth_px, b->depth_px, e),
          .opacity = 1.0,
      });
      break;
    }
    case MathMorphRole::enter: {
      const MathAnimNode *b = require_node(to, pair.to_id, &lookup_err);
      if (b == nullptr) {
        return std::unexpected(lookup_err);
      }
      if (!is_leaf_token(*b)) {
        break;
      }
      out.push_back(MathMorphToken{
          .role = MathMorphRole::enter,
          .correspondence_key = pair.correspondence_key,
          .owner_id = pair.to_id,
          .glyph_text = b->glyph_text,
          .x_px = b->x_px,
          .baseline_y_px = b->baseline_y_px,
          .width_px = b->width_px,
          .height_px = b->height_px,
          .depth_px = b->depth_px,
          .opacity = e,
      });
      break;
    }
    case MathMorphRole::exit: {
      const MathAnimNode *a = require_node(from, pair.from_id, &lookup_err);
      if (a == nullptr) {
        return std::unexpected(lookup_err);
      }
      if (!is_leaf_token(*a)) {
        break;
      }
      out.push_back(MathMorphToken{
          .role = MathMorphRole::exit,
          .correspondence_key = pair.correspondence_key,
          .owner_id = pair.from_id,
          .glyph_text = a->glyph_text,
          .x_px = a->x_px,
          .baseline_y_px = a->baseline_y_px,
          .width_px = a->width_px,
          .height_px = a->height_px,
          .depth_px = a->depth_px,
          .opacity = 1.0 - e,
      });
      break;
    }
    }
  }

  std::sort(out.begin(), out.end(),
            [](const MathMorphToken &a, const MathMorphToken &b) {
              return a.correspondence_key < b.correspondence_key;
            });
  return out;
}

namespace {

[[nodiscard]] const char *role_name(MathMorphRole role) noexcept {
  switch (role) {
  case MathMorphRole::persist:
    return "persist";
  case MathMorphRole::enter:
    return "enter";
  case MathMorphRole::exit:
    return "exit";
  }
  return "unknown";
}

void append_token_lines(std::ostringstream &oss, std::string_view heading,
                        const std::vector<MathMorphToken> &tokens) {
  oss << heading << '\n';
  for (const MathMorphToken &t : tokens) {
    oss << "  " << role_name(t.role) << ' ' << t.correspondence_key
        << " glyph=" << t.glyph_text << std::fixed << std::setprecision(3)
        << " x=" << t.x_px << " y=" << t.baseline_y_px << " w=" << t.width_px
        << " h=" << t.height_px << " d=" << t.depth_px << " op=" << t.opacity
        << '\n';
  }
}

} // namespace

std::string format_math_morph_golden(std::string_view label,
                                     const MathMorphPlan &plan,
                                     const std::vector<MathMorphToken> &at0,
                                     const std::vector<MathMorphToken> &at_mid,
                                     const std::vector<MathMorphToken> &at1) {
  std::ostringstream oss;
  oss << "morph " << label << '\n';
  oss << "pairs " << plan.pairs.size() << '\n';
  for (const MathMorphPair &p : plan.pairs) {
    oss << "  " << role_name(p.role) << ' ' << p.correspondence_key
        << " from=" << (p.from_id.empty() ? "-" : p.from_id)
        << " to=" << (p.to_id.empty() ? "-" : p.to_id) << '\n';
  }
  append_token_lines(oss, "t=0", at0);
  append_token_lines(oss, "t=0.5", at_mid);
  append_token_lines(oss, "t=1", at1);
  return oss.str();
}

} // namespace instantee::math
