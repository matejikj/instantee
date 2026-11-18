#include <instantee/render_core/paper_background_kit.hpp>

#include <instantee/core_model/deterministic_rng.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace instantee::render_core {
namespace {

diagnostics::Error make_err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "paper_background_kit", message};
}

[[nodiscard]] double unit(std::uint64_t seed, std::string_view channel,
                          std::uint64_t index) noexcept {
  return core::sample_unit_noise(seed, "background_kit", channel, index);
}

[[nodiscard]] std::uint32_t count_in_range(std::uint64_t seed,
                                           std::string_view channel,
                                           std::uint32_t min_v,
                                           std::uint32_t max_v) noexcept {
  if (max_v < min_v) {
    return min_v;
  }
  if (max_v == min_v) {
    return min_v;
  }
  const double u = unit(seed, channel, 0);
  const auto span = static_cast<double>(max_v - min_v + 1U);
  const auto offset = static_cast<std::uint32_t>(u * span);
  return min_v + std::min(offset, max_v - min_v);
}

[[nodiscard]] BackgroundSilhouettePart part(std::string id,
                                            PaperPropPartShape shape, float x,
                                            float y, float w, float h,
                                            std::string slot) {
  return BackgroundSilhouettePart{
      .id = std::move(id),
      .shape = shape,
      .local_bounds = Rect2f{x, y, w, h},
      .palette_slot = std::move(slot),
  };
}

[[nodiscard]] std::vector<BackgroundSilhouettePart>
make_mountain_silhouette(float w, float h) {
  return {
      part("ridge", PaperPropPartShape::rect, w * 0.05f, h * 0.35f, w * 0.90f,
           h * 0.55f, "body"),
      part("peak", PaperPropPartShape::rect, w * 0.35f, h * 0.10f, w * 0.30f,
           h * 0.30f, "accent"),
  };
}

[[nodiscard]] std::vector<BackgroundSilhouettePart>
make_tree_silhouette(float w, float h) {
  return {
      part("canopy", PaperPropPartShape::ellipse, w * 0.15f, h * 0.05f,
           w * 0.70f, h * 0.55f, "accent"),
      part("trunk", PaperPropPartShape::rect, w * 0.42f, h * 0.55f, w * 0.16f,
           h * 0.40f, "ink"),
  };
}

[[nodiscard]] std::vector<BackgroundSilhouettePart>
make_atmosphere_silhouette(float w, float h) {
  return {
      part("haze", PaperPropPartShape::ellipse, w * 0.05f, h * 0.20f, w * 0.90f,
           h * 0.55f, "sky"),
  };
}

[[nodiscard]] std::expected<ResolvedPaperProp, diagnostics::Error>
make_scaled_prop(PaperPropKind kind, float scale) {
  const auto palette = make_default_paper_prop_palette();
  const auto materials = make_default_paper_prop_materials();
  PaperPropBlueprint blueprint;
  switch (kind) {
  case PaperPropKind::building:
    blueprint = make_building_prop_blueprint(100.0f, 140.0f, scale);
    break;
  case PaperPropKind::cloud:
    blueprint = make_cloud_prop_blueprint(140.0f, 70.0f, scale);
    break;
  case PaperPropKind::instrument:
    blueprint = make_instrument_prop_blueprint(120.0f, 80.0f, scale);
    break;
  }
  return instantiate_paper_prop(blueprint, palette, materials);
}

} // namespace

BackgroundKitLimits background_kit_limits_v1() noexcept {
  return BackgroundKitLimits{};
}

std::expected<void, diagnostics::Error>
validate_background_kit_params(const BackgroundKitParams &params) {
  if (!(params.stage_width > 0.0) || !(params.stage_height > 0.0) ||
      !std::isfinite(params.stage_width) ||
      !std::isfinite(params.stage_height)) {
    return std::unexpected(
        make_err("invalid_stage", "Stage size must be finite and > 0"));
  }
  const auto &lim = params.limits;
  if (lim.max_total_assets == 0 || lim.max_mountains == 0 ||
      lim.max_clouds == 0 || lim.max_buildings == 0 || lim.max_trees == 0 ||
      lim.max_atmosphere == 0) {
    return std::unexpected(
        make_err("invalid_limits", "Background kit limits must be non-zero"));
  }
  if (lim.stage_layers <
          static_cast<std::uint32_t>(core::kDepthStageMinLayers) ||
      lim.stage_layers >
          static_cast<std::uint32_t>(core::kDepthStageMaxLayers)) {
    return std::unexpected(
        make_err("invalid_stage_layers",
                 "stage_layers must be within depth-stage bounds"));
  }
  return {};
}

std::expected<BackgroundScenery, diagnostics::Error>
generate_paper_background_scenery(const BackgroundKitParams &params) {
  if (auto ok = validate_background_kit_params(params); !ok) {
    return std::unexpected(ok.error());
  }

  core::DepthStageSpacing spacing{};
  spacing.near_depth = 4.0;
  spacing.depth_step = 1.6;
  spacing.plane_width = params.stage_width;
  spacing.plane_height = params.stage_height;
  spacing.origin = {0.5 * params.stage_width, 0.5 * params.stage_height};
  auto stage =
      core::make_layered_depth_stage(params.limits.stage_layers, spacing);
  if (!stage) {
    return std::unexpected(stage.error());
  }

  const auto &lim = params.limits;
  // Leave headroom under the total cap across kinds.
  const std::uint32_t mountains =
      count_in_range(params.seed, "count_mountain", 2, lim.max_mountains);
  const std::uint32_t clouds =
      count_in_range(params.seed, "count_cloud", 2, lim.max_clouds);
  const std::uint32_t buildings =
      count_in_range(params.seed, "count_building", 2, lim.max_buildings);
  const std::uint32_t trees =
      count_in_range(params.seed, "count_tree", 2, lim.max_trees);
  const std::uint32_t atmosphere =
      count_in_range(params.seed, "count_atmosphere", 1, lim.max_atmosphere);

  struct Planned {
    BackgroundElementKind kind;
    std::uint32_t count;
    std::string_view channel;
  };
  const Planned planned[] = {
      {BackgroundElementKind::mountain, mountains, "mountain"},
      {BackgroundElementKind::cloud, clouds, "cloud"},
      {BackgroundElementKind::building, buildings, "building"},
      {BackgroundElementKind::tree, trees, "tree"},
      {BackgroundElementKind::atmosphere, atmosphere, "atmosphere"},
  };

  BackgroundScenery scenery;
  scenery.seed = params.seed;
  scenery.stage = std::move(*stage);
  scenery.elements.reserve(lim.max_total_assets);

  std::uint32_t global_index = 0;
  for (const auto &plan : planned) {
    for (std::uint32_t i = 0; i < plan.count; ++i) {
      if (scenery.elements.size() >= lim.max_total_assets) {
        break;
      }
      const auto idx = static_cast<std::uint64_t>(global_index);
      BackgroundElement el;
      el.kind = plan.kind;
      el.id = std::string(plan.channel) + "_" + std::to_string(i);
      const auto layer_u =
          unit(params.seed, std::string(plan.channel) + "_layer", idx);
      el.stage_layer = static_cast<std::uint32_t>(
          layer_u * static_cast<double>(scenery.stage.planes.size()));
      if (el.stage_layer >= scenery.stage.planes.size()) {
        el.stage_layer =
            static_cast<std::uint32_t>(scenery.stage.planes.size() - 1U);
      }
      el.depth = scenery.stage.planes[el.stage_layer].depth;
      el.offset_x =
          (unit(params.seed, std::string(plan.channel) + "_x", idx) - 0.5) *
          params.stage_width * 0.8;
      el.offset_y =
          (unit(params.seed, std::string(plan.channel) + "_y", idx) - 0.5) *
          params.stage_height * 0.35;
      const double scale_u =
          unit(params.seed, std::string(plan.channel) + "_scale", idx);
      el.scale = static_cast<float>(0.55 + 0.7 * scale_u);

      if (plan.kind == BackgroundElementKind::cloud ||
          plan.kind == BackgroundElementKind::building) {
        const auto prop_kind = plan.kind == BackgroundElementKind::cloud
                                   ? PaperPropKind::cloud
                                   : PaperPropKind::building;
        auto prop = make_scaled_prop(prop_kind, el.scale);
        if (!prop) {
          return std::unexpected(prop.error());
        }
        el.prop = std::move(*prop);
        el.has_prop = true;
      } else if (plan.kind == BackgroundElementKind::mountain) {
        el.silhouette =
            make_mountain_silhouette(160.0f * el.scale, 90.0f * el.scale);
      } else if (plan.kind == BackgroundElementKind::tree) {
        el.silhouette =
            make_tree_silhouette(40.0f * el.scale, 70.0f * el.scale);
      } else {
        el.silhouette =
            make_atmosphere_silhouette(200.0f * el.scale, 60.0f * el.scale);
      }

      scenery.elements.push_back(std::move(el));
      ++global_index;
    }
  }

  // Stable order: depth ascending, then kind id, then element id.
  std::sort(scenery.elements.begin(), scenery.elements.end(),
            [](const BackgroundElement &a, const BackgroundElement &b) {
              if (!core::nearly_equal(a.depth, b.depth)) {
                return a.depth < b.depth;
              }
              if (a.kind != b.kind) {
                return static_cast<std::uint8_t>(a.kind) <
                       static_cast<std::uint8_t>(b.kind);
              }
              return a.id < b.id;
            });

  scenery.asset_count = static_cast<std::uint32_t>(scenery.elements.size());
  if (scenery.asset_count > lim.max_total_assets) {
    return std::unexpected(
        make_err("asset_overflow", "Generated asset count exceeds total cap"));
  }
  if (auto ok = core::validate_depth_stage(scenery.stage); !ok) {
    return std::unexpected(ok.error());
  }
  return scenery;
}

std::string format_paper_background_scenery(std::string_view label,
                                            const BackgroundScenery &scenery) {
  std::ostringstream os;
  os << "paper_background " << label << '\n';
  os << "seed=" << scenery.seed << " assets=" << scenery.asset_count
     << " layers=" << scenery.stage.planes.size() << '\n';
  // Counts by kind (stable enumeration order).
  std::uint32_t counts[6] = {};
  for (const auto &el : scenery.elements) {
    const auto k = static_cast<std::uint8_t>(el.kind);
    if (k < 6U) {
      ++counts[k];
    }
  }
  os << "counts mountain=" << counts[1] << " cloud=" << counts[2]
     << " building=" << counts[3] << " tree=" << counts[4]
     << " atmosphere=" << counts[5] << '\n';
  os << std::fixed << std::setprecision(4);
  for (const auto &el : scenery.elements) {
    os << "e " << el.id << " kind=" << background_element_kind_id(el.kind)
       << " layer=" << el.stage_layer << " depth=" << el.depth
       << " x=" << el.offset_x << " y=" << el.offset_y << " scale=" << el.scale;
    if (el.has_prop) {
      os << " prop=" << el.prop.blueprint_id
         << " parts=" << el.prop.parts.size();
    } else {
      os << " sil=" << el.silhouette.size();
    }
    os << '\n';
  }
  return os.str();
}

} // namespace instantee::render_core
