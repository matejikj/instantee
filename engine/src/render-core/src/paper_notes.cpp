#include <instantee/render_core/paper_notes.hpp>

#include <instantee/render_core/primitives.hpp>
#include <instantee/render_core/renderer.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>

namespace instantee::render_core {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "paper_notes", message};
}

[[nodiscard]] float clamp01(float v) noexcept {
  if (!(v > 0.0f)) {
    return 0.0f;
  }
  if (v > 1.0f) {
    return 1.0f;
  }
  return v;
}

[[nodiscard]] bool unit01(float v) noexcept {
  return std::isfinite(v) && v >= 0.0f && v <= 1.0f;
}

[[nodiscard]] Rgba8 mix_wash(Rgba8 base, Rgba8 wash, float amount) noexcept {
  const float t = clamp01(amount);
  auto ch = [t](std::uint8_t a, std::uint8_t b) {
    return static_cast<std::uint8_t>(std::lround(
        static_cast<float>(a) * (1.0f - t) + static_cast<float>(b) * t));
  };
  return Rgba8{ch(base.r, wash.r), ch(base.g, wash.g), ch(base.b, wash.b),
               base.a};
}

[[nodiscard]] Rgba8 scale_alpha(Rgba8 c, float reveal) noexcept {
  const float a = static_cast<float>(c.a) * clamp01(reveal);
  c.a = static_cast<std::uint8_t>(std::lround(std::min(255.0f, a)));
  return c;
}

[[nodiscard]] DrawOp make_fill_rounded(Rect2f bounds, float radius,
                                       Rgba8 color) {
  return DrawOp{.kind = DrawOpKind::fill_rounded_rect,
                .rect = bounds,
                .color = color,
                .corner_radius = radius};
}

[[nodiscard]] DrawOp make_line(float x0, float y0, float x1, float y1,
                               float width, Rgba8 color) {
  return DrawOp{.kind = DrawOpKind::stroke_line,
                .rect = {.x = x0, .y = y0, .width = 0, .height = 0},
                .x1 = x1,
                .y1 = y1,
                .color = color,
                .stroke_width = width};
}

[[nodiscard]] bool finite_rect(const Rect2f &r) noexcept {
  return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) &&
         std::isfinite(r.height) && r.width > 0.0f && r.height > 0.0f;
}

struct ResolvedCard {
  Rect2f body{};
  float corner = 0;
  float uncover = 1;
  float attach = 1;
  PaperSheetParams paper{};
  ShadowParams shadow{};
  bool ruled = false;
  float rule_spacing = 10;
  Rgba8 rule_color{};
  bool has_photo = false;
  Rect2f photo{};
  float photo_corner = 1;
  Rgba8 photo_color{};
};

[[nodiscard]] std::expected<ResolvedCard, diagnostics::Error>
resolve_sticky(const StickyNoteSpec &n) {
  if (n.id.empty() || !finite_rect(n.bounds) ||
      !std::isfinite(n.corner_radius) || n.corner_radius < 0.0f) {
    return std::unexpected(
        make_invalid("paper_notes.bad_sticky", "sticky note fields invalid"));
  }
  if (auto ok = validate_paper_sheet_params(n.paper); !ok) {
    return std::unexpected(ok.error());
  }
  if (!unit01(n.motion.attach) || !unit01(n.motion.uncover) ||
      !std::isfinite(n.motion.peel_lift_x_px) ||
      !std::isfinite(n.motion.peel_lift_y_px)) {
    return std::unexpected(
        make_invalid("paper_notes.bad_motion", "motion fields invalid"));
  }
  const float peel = 1.0f - n.motion.attach;
  ResolvedCard out;
  out.body = n.bounds;
  out.body.x += n.motion.peel_lift_x_px * peel;
  out.body.y += n.motion.peel_lift_y_px * peel;
  out.corner = n.corner_radius;
  out.uncover = n.motion.uncover;
  out.attach = n.motion.attach;
  out.paper = n.paper;
  out.shadow = n.shadow;
  // Lifted cards cast a longer, softer-looking hard offset (CPU path stays
  // hard).
  out.shadow.offset_x_px *= (1.0f + peel * n.paper.shadow_bias);
  out.shadow.offset_y_px *= (1.0f + peel * n.paper.shadow_bias);
  out.shadow.color = scale_alpha(out.shadow.color, n.motion.attach);
  return out;
}

[[nodiscard]] std::expected<ResolvedCard, diagnostics::Error>
resolve_index(const IndexCardSpec &n) {
  if (n.id.empty() || !finite_rect(n.bounds) ||
      !std::isfinite(n.corner_radius) || n.corner_radius < 0.0f ||
      !std::isfinite(n.rule_spacing_px) || !(n.rule_spacing_px > 0.0f)) {
    return std::unexpected(
        make_invalid("paper_notes.bad_index", "index card fields invalid"));
  }
  if (auto ok = validate_paper_sheet_params(n.paper); !ok) {
    return std::unexpected(ok.error());
  }
  if (!unit01(n.motion.attach) || !unit01(n.motion.uncover) ||
      !std::isfinite(n.motion.peel_lift_x_px) ||
      !std::isfinite(n.motion.peel_lift_y_px)) {
    return std::unexpected(
        make_invalid("paper_notes.bad_motion", "motion fields invalid"));
  }
  const float peel = 1.0f - n.motion.attach;
  ResolvedCard out;
  out.body = n.bounds;
  out.body.x += n.motion.peel_lift_x_px * peel;
  out.body.y += n.motion.peel_lift_y_px * peel;
  out.corner = n.corner_radius;
  out.uncover = n.motion.uncover;
  out.attach = n.motion.attach;
  out.paper = n.paper;
  out.shadow = n.shadow;
  out.shadow.offset_x_px *= (1.0f + peel * n.paper.shadow_bias);
  out.shadow.offset_y_px *= (1.0f + peel * n.paper.shadow_bias);
  out.shadow.color = scale_alpha(out.shadow.color, n.motion.attach);
  out.ruled = n.ruled_lines;
  out.rule_spacing = n.rule_spacing_px;
  out.rule_color = n.rule_color;
  return out;
}

[[nodiscard]] std::expected<ResolvedCard, diagnostics::Error>
resolve_mount(const PhotoMountSpec &n) {
  if (n.id.empty() || !finite_rect(n.mount_bounds) ||
      !finite_rect(n.photo_frame) || !std::isfinite(n.mat_corner_radius) ||
      n.mat_corner_radius < 0.0f || !std::isfinite(n.photo_corner_radius) ||
      n.photo_corner_radius < 0.0f) {
    return std::unexpected(
        make_invalid("paper_notes.bad_mount", "photo mount fields invalid"));
  }
  // Photo window must sit inside the mat.
  if (n.photo_frame.x < n.mount_bounds.x ||
      n.photo_frame.y < n.mount_bounds.y ||
      n.photo_frame.x + n.photo_frame.width >
          n.mount_bounds.x + n.mount_bounds.width + 1.0e-3f ||
      n.photo_frame.y + n.photo_frame.height >
          n.mount_bounds.y + n.mount_bounds.height + 1.0e-3f) {
    return std::unexpected(make_invalid("paper_notes.photo_outside",
                                        "photo frame must sit inside the mat"));
  }
  if (auto ok = validate_paper_sheet_params(n.paper); !ok) {
    return std::unexpected(ok.error());
  }
  if (!unit01(n.motion.attach) || !unit01(n.motion.uncover) ||
      !std::isfinite(n.motion.peel_lift_x_px) ||
      !std::isfinite(n.motion.peel_lift_y_px)) {
    return std::unexpected(
        make_invalid("paper_notes.bad_motion", "motion fields invalid"));
  }
  const float peel = 1.0f - n.motion.attach;
  ResolvedCard out;
  out.body = n.mount_bounds;
  out.body.x += n.motion.peel_lift_x_px * peel;
  out.body.y += n.motion.peel_lift_y_px * peel;
  out.corner = n.mat_corner_radius;
  out.uncover = n.motion.uncover;
  out.attach = n.motion.attach;
  out.paper = n.paper;
  out.shadow = n.shadow;
  out.shadow.offset_x_px *= (1.0f + peel * n.paper.shadow_bias);
  out.shadow.offset_y_px *= (1.0f + peel * n.paper.shadow_bias);
  out.shadow.color = scale_alpha(out.shadow.color, n.motion.attach);
  out.has_photo = true;
  out.photo = n.photo_frame;
  out.photo.x += n.motion.peel_lift_x_px * peel;
  out.photo.y += n.motion.peel_lift_y_px * peel;
  out.photo_corner = n.photo_corner_radius;
  out.photo_color = n.photo_color;
  return out;
}

void append_edge(std::vector<DrawOp> &ops, const Rect2f &body, float thickness,
                 Rgba8 color) {
  if (!(thickness > 0.0f) || color.a == 0) {
    return;
  }
  const float x0 = body.x;
  const float y0 = body.y;
  const float x1 = body.x + body.width;
  const float y1 = body.y + body.height;
  ops.push_back(make_line(x0, y0, x1, y0, thickness, color));
  ops.push_back(make_line(x1, y0, x1, y1, thickness, color));
  ops.push_back(make_line(x1, y1, x0, y1, thickness, color));
  ops.push_back(make_line(x0, y1, x0, y0, thickness, color));
}

void append_card(std::vector<DrawOp> &ops, const ResolvedCard &card) {
  if (card.attach <= 0.0f && card.uncover <= 0.0f) {
    return;
  }
  const float visible_h = card.body.height * clamp01(card.uncover);
  if (!(visible_h > 0.0f)) {
    return;
  }
  Rect2f visible = card.body;
  visible.height = visible_h;

  if (card.shadow.enabled && card.shadow.color.a > 0) {
    Rect2f shadow_r = visible;
    shadow_r.x += card.shadow.offset_x_px;
    shadow_r.y += card.shadow.offset_y_px;
    ops.push_back(make_fill_rounded(shadow_r, card.corner, card.shadow.color));
  }

  const Rgba8 fill =
      scale_alpha(mix_wash(card.paper.base_color, card.paper.wash_color,
                           card.paper.color_wash),
                  card.attach);
  ops.push_back(make_fill_rounded(visible, card.corner, fill));
  append_edge(ops, visible, card.paper.edge_thickness_px,
              scale_alpha(card.paper.edge_color, card.attach));

  if (card.ruled) {
    const float top = visible.y + card.rule_spacing;
    const float bottom = visible.y + visible.height - 2.0f;
    for (float y = top; y < bottom; y += card.rule_spacing) {
      ops.push_back(make_line(visible.x + 4.0f, y,
                              visible.x + visible.width - 4.0f, y, 1.0f,
                              scale_alpha(card.rule_color, card.attach)));
    }
  }

  if (card.has_photo) {
    Rect2f photo = card.photo;
    const float photo_bottom = card.photo.y + card.photo.height;
    const float clip_bottom = visible.y + visible.height;
    if (photo.y >= clip_bottom) {
      return;
    }
    if (photo_bottom > clip_bottom) {
      photo.height = std::max(0.0f, clip_bottom - photo.y);
    }
    if (photo.height > 0.0f) {
      ops.push_back(
          make_fill_rounded(photo, card.photo_corner,
                            scale_alpha(card.photo_color, card.attach)));
    }
  }
}

} // namespace

std::expected<void, diagnostics::Error>
validate_paper_sheet_params(const PaperSheetParams &params) {
  if (!unit01(params.fiber) || !unit01(params.roughness) ||
      !unit01(params.color_wash) || !unit01(params.shadow_bias)) {
    return std::unexpected(
        make_invalid("paper_notes.bad_paper",
                     "fiber/roughness/wash/shadow_bias must be in [0,1]"));
  }
  if (!std::isfinite(params.edge_thickness_px) ||
      params.edge_thickness_px < 0.0f) {
    return std::unexpected(make_invalid(
        "paper_notes.bad_paper", "edge_thickness_px must be finite and >= 0"));
  }
  return {};
}

ShaderFeatureRegistry make_paper_note_shader_feature_registry() {
  auto registry = make_shadow_shader_feature_registry();

  ShaderFeatureDesc sheet;
  sheet.id =
      ShaderFeatureId{.name = std::string{kPaperSheetFeature}, .version = 1};
  sheet.params.push_back(ShaderParamDesc{
      .name = "fiber",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 0.35f},
  });
  sheet.params.push_back(ShaderParamDesc{
      .name = "roughness",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 0.25f},
  });
  sheet.params.push_back(ShaderParamDesc{
      .name = "color_wash",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 0.12f},
  });
  sheet.params.push_back(ShaderParamDesc{
      .name = "edge_thickness_px",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 1.25f},
  });
  sheet.params.push_back(ShaderParamDesc{
      .name = "shadow_bias",
      .default_value = ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                          .scalar_f32 = 0.55f},
  });
  sheet.pass_contributions.push_back(PassContribution{
      .pass_key = std::string{kPaperSheetPass}, .sort_key = 40});
  sheet.capabilities.require_apple7 = true;
  (void)registry.register_feature(std::move(sheet));
  return registry;
}

std::expected<PaperNotePassPlan, diagnostics::Error>
plan_paper_note_passes(const ShaderFeatureRegistry &registry,
                       const MaterialDesc &material,
                       const std::vector<std::string> &active_feature_names) {
  auto planned = registry.plan_passes(material, active_feature_names);
  if (!planned) {
    return std::unexpected(planned.error());
  }
  PaperNotePassPlan out;
  out.pass_keys = planned->pass_keys;
  for (const auto &key : out.pass_keys) {
    if (key == kPaperSheetPass) {
      out.paper_sheet_active = true;
    }
    if (key == kShadowCastPass || key == kShadowBlurPass) {
      out.shadow_active = true;
    }
  }
  return out;
}

std::expected<PassDesc, diagnostics::Error>
compose_paper_note_pass(const PaperNoteScene &scene) {
  if (scene.width_px == 0 || scene.height_px == 0) {
    return std::unexpected(
        make_invalid("paper_notes.bad_scene", "scene size must be > 0"));
  }
  PassDesc pass;
  pass.width_px = scene.width_px;
  pass.height_px = scene.height_px;
  pass.color_target = 1;
  pass.ops.push_back(
      DrawOp{.kind = DrawOpKind::clear, .color = scene.clear_color});

  for (const auto &note : scene.notes) {
    auto resolved = std::visit(
        [](const auto &spec)
            -> std::expected<ResolvedCard, diagnostics::Error> {
          using T = std::decay_t<decltype(spec)>;
          if constexpr (std::is_same_v<T, StickyNoteSpec>) {
            return resolve_sticky(spec);
          } else if constexpr (std::is_same_v<T, IndexCardSpec>) {
            return resolve_index(spec);
          } else {
            return resolve_mount(spec);
          }
        },
        note);
    if (!resolved) {
      return std::unexpected(resolved.error());
    }
    append_card(pass.ops, *resolved);
  }
  if (auto valid = validate_pass(pass); !valid) {
    return std::unexpected(valid.error());
  }
  return pass;
}

std::expected<Rgba8Image, diagnostics::Error>
rasterize_paper_note_scene(const PaperNoteScene &scene) {
  auto pass = compose_paper_note_pass(scene);
  if (!pass) {
    return std::unexpected(pass.error());
  }
  Rgba8Image image;
  if (auto ok = rasterize_pass(image, *pass); !ok) {
    return std::unexpected(ok.error());
  }
  return image;
}

std::string format_paper_note_golden(std::string_view label,
                                     const PaperNoteScene &scene,
                                     const PassDesc &pass,
                                     const Rgba8Image &img) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3);
  oss << "paper-notes " << label << '\n';
  oss << "size " << scene.width_px << "x" << scene.height_px
      << " notes=" << scene.notes.size() << " ops=" << pass.ops.size() << '\n';
  for (const auto &note : scene.notes) {
    oss << "kind=" << paper_note_kind_id(note) << '\n';
    std::visit(
        [&](const auto &spec) {
          using T = std::decay_t<decltype(spec)>;
          oss << "  id=" << spec.id << " attach=" << spec.motion.attach
              << " uncover=" << spec.motion.uncover << '\n';
          if constexpr (std::is_same_v<T, StickyNoteSpec>) {
            oss << "  bounds=" << spec.bounds.x << "," << spec.bounds.y << ","
                << spec.bounds.width << "x" << spec.bounds.height << '\n';
          } else if constexpr (std::is_same_v<T, IndexCardSpec>) {
            oss << "  bounds=" << spec.bounds.x << "," << spec.bounds.y << ","
                << spec.bounds.width << "x" << spec.bounds.height
                << " ruled=" << (spec.ruled_lines ? 1 : 0) << '\n';
          } else {
            oss << "  mount=" << spec.mount_bounds.x << ","
                << spec.mount_bounds.y << "," << spec.mount_bounds.width << "x"
                << spec.mount_bounds.height << " photo=" << spec.photo_frame.x
                << "," << spec.photo_frame.y << "," << spec.photo_frame.width
                << "x" << spec.photo_frame.height << '\n';
          }
        },
        note);
  }
  oss << "ops\n";
  for (const auto &op : pass.ops) {
    oss << "  kind=" << static_cast<int>(op.kind);
    if (op.kind == DrawOpKind::stroke_line) {
      oss << " line=" << op.rect.x << "," << op.rect.y << "->" << op.x1 << ","
          << op.y1 << " w=" << op.stroke_width;
    } else if (op.kind != DrawOpKind::clear) {
      oss << " rect=" << op.rect.x << "," << op.rect.y << "," << op.rect.width
          << "x" << op.rect.height;
    }
    oss << " a=" << static_cast<int>(op.color.a) << '\n';
  }
  const std::uint32_t samples[][2] = {{8, 8},   {40, 24}, {72, 40},
                                      {20, 60}, {90, 70}, {50, 50}};
  for (const auto &s : samples) {
    if (s[0] >= img.width_px || s[1] >= img.height_px) {
      continue;
    }
    const std::size_t idx =
        (static_cast<std::size_t>(s[1]) * img.width_px + s[0]) * 4U;
    oss << "p" << s[0] << "," << s[1] << "=" << static_cast<int>(img.rgba[idx])
        << "," << static_cast<int>(img.rgba[idx + 1]) << ","
        << static_cast<int>(img.rgba[idx + 2]) << ","
        << static_cast<int>(img.rgba[idx + 3]) << '\n';
  }
  return oss.str();
}

} // namespace instantee::render_core
