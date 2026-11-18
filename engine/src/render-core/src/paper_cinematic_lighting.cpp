#include <instantee/render_core/paper_cinematic_lighting.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace instantee::render_core {
namespace {

diagnostics::Error make_err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "paper_cinematic_lighting", message};
}

[[nodiscard]] bool finite_nonneg(float v) noexcept {
  return std::isfinite(v) && v >= 0.0f;
}

[[nodiscard]] bool valid_rect(const Rect2f &r) noexcept {
  return is_valid_rect(r) && std::isfinite(r.x) && std::isfinite(r.y);
}

[[nodiscard]] std::uint8_t scale_alpha(std::uint8_t a, float opacity) noexcept {
  opacity = std::clamp(opacity, 0.0f, 1.0f);
  return static_cast<std::uint8_t>(
      std::lround(static_cast<float>(a) * opacity));
}

[[nodiscard]] GraphPassNode make_clear_pass(std::uint64_t target,
                                            std::uint32_t w, std::uint32_t h,
                                            Rgba8 clear) {
  GraphPassNode node{};
  node.pass.width_px = w;
  node.pass.height_px = h;
  node.pass.color_target = target;
  node.pass.ops.push_back(DrawOp{.kind = DrawOpKind::clear, .color = clear});
  node.resources.push_back(GraphResourceEdge{
      .resource = target, .usage = ResourceUsage::color_write});
  return node;
}

} // namespace

std::expected<PaperCinematicLightingRecipe, diagnostics::Error>
parse_paper_cinematic_lighting_recipe(std::string_view text) {
  if (text == kPaperLightingRevealKeyFill || text == "reveal_key_fill") {
    return PaperCinematicLightingRecipe::reveal_key_fill;
  }
  if (text == kPaperLightingCloseupRim || text == "closeup_rim") {
    return PaperCinematicLightingRecipe::closeup_rim;
  }
  if (text == kPaperLightingDialogueSoft || text == "dialogue_soft") {
    return PaperCinematicLightingRecipe::dialogue_soft;
  }
  if (text == kPaperLightingTechnicalFlat || text == "technical_flat") {
    return PaperCinematicLightingRecipe::technical_flat;
  }
  if (text == kPaperLightingAtmosphericVeil || text == "atmospheric_veil") {
    return PaperCinematicLightingRecipe::atmospheric_veil;
  }
  return std::unexpected(
      make_err("unknown_lighting_recipe", "Unknown paper cinematic lighting"));
}

PaperCinematicLightingParams
make_paper_cinematic_lighting(PaperCinematicLightingRecipe recipe) noexcept {
  PaperCinematicLightingParams p;
  p.recipe = recipe;
  p.active_feature_ids = {
      std::string(paper_cinematic_lighting_recipe_id(recipe)),
      std::string(kDropShadowFeature)};
  switch (recipe) {
  case PaperCinematicLightingRecipe::reveal_key_fill:
    p.clear_color = Rgba8{236, 226, 208, 255};
    p.key_color = Rgba8{255, 250, 238, 255};
    p.fill_color = Rgba8{198, 186, 170, 255};
    p.shadow.offset_x_px = 10.0f;
    p.shadow.offset_y_px = 12.0f;
    p.shadow.soft_radius_px = 2.5f;
    p.shadow.shadow_color = Rgba8{0, 0, 0, 100};
    break;
  case PaperCinematicLightingRecipe::closeup_rim:
    p.clear_color = Rgba8{28, 30, 36, 255};
    p.key_color = Rgba8{255, 236, 210, 255};
    p.fill_color = Rgba8{64, 70, 82, 255};
    p.shadow.offset_x_px = -6.0f;
    p.shadow.offset_y_px = 4.0f;
    p.shadow.soft_radius_px = 1.5f;
    p.shadow.shadow_color = Rgba8{0, 0, 0, 140};
    break;
  case PaperCinematicLightingRecipe::dialogue_soft:
    p.clear_color = Rgba8{244, 236, 224, 255};
    p.key_color = Rgba8{252, 246, 236, 255};
    p.fill_color = Rgba8{220, 210, 196, 255};
    p.shadow.offset_x_px = 4.0f;
    p.shadow.offset_y_px = 6.0f;
    p.shadow.soft_radius_px = 3.5f;
    p.shadow.max_soft_spread_px = 5.0f;
    p.shadow.shadow_color = Rgba8{0, 0, 0, 72};
    break;
  case PaperCinematicLightingRecipe::technical_flat:
    p.clear_color = Rgba8{250, 248, 242, 255};
    p.key_color = Rgba8{255, 255, 255, 255};
    p.fill_color = Rgba8{230, 228, 220, 255};
    p.shadow.offset_x_px = 2.0f;
    p.shadow.offset_y_px = 2.0f;
    p.shadow.soft_radius_px = 0.0f;
    p.shadow.shadow_color = Rgba8{0, 0, 0, 48};
    break;
  case PaperCinematicLightingRecipe::atmospheric_veil:
    p.clear_color = Rgba8{210, 214, 220, 255};
    p.key_color = Rgba8{236, 232, 224, 255};
    p.fill_color = Rgba8{180, 186, 196, 255};
    p.veil_color = Rgba8{36, 44, 58, 160};
    p.veil_opacity = 0.55f;
    p.shadow.offset_x_px = 8.0f;
    p.shadow.offset_y_px = 10.0f;
    p.shadow.soft_radius_px = 3.0f;
    p.shadow.shadow_color = Rgba8{0, 0, 0, 90};
    break;
  }
  return p;
}

ShaderFeatureRegistry make_paper_cinematic_lighting_feature_registry() {
  auto registry = make_shadow_shader_feature_registry();
  const PaperCinematicLightingRecipe recipes[] = {
      PaperCinematicLightingRecipe::reveal_key_fill,
      PaperCinematicLightingRecipe::closeup_rim,
      PaperCinematicLightingRecipe::dialogue_soft,
      PaperCinematicLightingRecipe::technical_flat,
      PaperCinematicLightingRecipe::atmospheric_veil,
  };
  for (const auto recipe : recipes) {
    ShaderFeatureDesc desc{};
    desc.id.name = std::string(paper_cinematic_lighting_recipe_id(recipe));
    desc.id.version = 1;
    // Empty contributions: recipe selection configures graph assembly params.
    (void)registry.register_feature(std::move(desc));
  }
  return registry;
}

std::expected<RenderGraphDesc, diagnostics::Error>
build_paper_cinematic_shot_graph(
    const PaperCinematicShotGraphRequest &request) {
  if (request.width_px == 0 || request.height_px == 0) {
    return std::unexpected(
        make_err("invalid_extent", "Shot graph requires positive extents"));
  }
  if (request.output_resource == 0 || request.shadow_resource == 0 ||
      request.output_resource == request.shadow_resource) {
    return std::unexpected(make_err(
        "invalid_resource", "Output and shadow resources must be distinct"));
  }
  if (!valid_rect(request.subject) || !valid_rect(request.support)) {
    return std::unexpected(
        make_err("invalid_rect", "Subject and support rects must be valid"));
  }
  if (!finite_nonneg(request.subject_depth) ||
      !finite_nonneg(request.support_depth) ||
      request.subject_depth >= request.support_depth) {
    return std::unexpected(make_err(
        "invalid_depth", "Subject must sit nearer than the support card"));
  }
  if (!std::isfinite(request.dissolve_progress) ||
      request.dissolve_progress < 0.0f || request.dissolve_progress > 1.0f) {
    return std::unexpected(
        make_err("invalid_dissolve", "dissolve_progress must be in [0, 1]"));
  }

  const auto lighting = make_paper_cinematic_lighting(request.recipe);
  const float veil =
      request.recipe == PaperCinematicLightingRecipe::atmospheric_veil
          ? std::max(lighting.veil_opacity, request.dissolve_progress)
          : 0.0f;

  // Shadow silhouette (single writer — graph forbids WAW on one resource).
  const Rect2f shadow_rect{
      .x = request.subject.x + lighting.shadow.offset_x_px,
      .y = request.subject.y + lighting.shadow.offset_y_px,
      .width = request.subject.width,
      .height = request.subject.height,
  };

  RenderGraphDesc graph;
  graph.output_resource = request.output_resource;

  GraphPassNode shadow =
      make_clear_pass(request.shadow_resource, request.width_px,
                      request.height_px, Rgba8{0, 0, 0, 0});
  shadow.pass.ops.push_back(DrawOp{.kind = DrawOpKind::fill_rect,
                                   .rect = shadow_rect,
                                   .color = lighting.shadow.shadow_color});
  graph.passes.push_back(std::move(shadow));

  // Main color: clear → support → subject → optional atmospheric veil.
  // Samples the shadow buffer so compile emits a write→sample barrier.
  GraphPassNode main =
      make_clear_pass(request.output_resource, request.width_px,
                      request.height_px, lighting.clear_color);
  main.pass.ops.push_back(DrawOp{.kind = DrawOpKind::fill_rect,
                                 .rect = request.support,
                                 .color = lighting.fill_color});
  main.pass.ops.push_back(DrawOp{.kind = DrawOpKind::fill_rect,
                                 .rect = request.subject,
                                 .color = lighting.key_color});
  if (veil > 0.0f) {
    Rgba8 veil_color = lighting.veil_color;
    const std::uint8_t base_a = lighting.veil_color.a == 0
                                    ? static_cast<std::uint8_t>(160)
                                    : lighting.veil_color.a;
    veil_color.a = scale_alpha(base_a, veil);
    main.pass.ops.push_back(DrawOp{
        .kind = DrawOpKind::fill_rect,
        .rect = Rect2f{.x = 0,
                       .y = 0,
                       .width = static_cast<float>(request.width_px),
                       .height = static_cast<float>(request.height_px)},
        .color = veil_color,
    });
  }
  main.resources.push_back(GraphResourceEdge{
      .resource = request.shadow_resource, .usage = ResourceUsage::sample});
  graph.passes.push_back(std::move(main));

  return graph;
}

std::string format_paper_cinematic_shot_graph_manifest(
    const PaperCinematicShotGraphRequest &request, const RenderGraphDesc &graph,
    const CompiledRenderGraph &compiled) {
  std::ostringstream os;
  os << "paper_cinematic_shot_graph\n";
  os << "recipe=" << paper_cinematic_lighting_recipe_id(request.recipe) << '\n';
  os << "extent=" << request.width_px << 'x' << request.height_px << '\n';
  os << "passes_desc=" << graph.passes.size()
     << " passes_live=" << compiled.pass_order.size()
     << " barriers=" << compiled.barriers.size() << '\n';
  os << "output=" << compiled.output_resource << '\n';
  os << "dissolve=" << request.dissolve_progress << '\n';
  return os.str();
}

} // namespace instantee::render_core
