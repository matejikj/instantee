#include <instantee/render_core/paper_layered_shadow.hpp>

#include <instantee/render_core/primitives.hpp>
#include <instantee/render_core/renderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace instantee::render_core {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "paper_layered_shadow", message};
}

[[nodiscard]] bool finite_nonneg(float v) noexcept {
  return std::isfinite(v) && v >= 0.0f;
}

void blend_pixel(std::uint8_t *dst, Rgba8 src, float coverage) noexcept {
  coverage = std::max(0.0f, std::min(1.0f, coverage));
  if (coverage <= 0.0f) {
    return;
  }
  const float sa = (static_cast<float>(src.a) / 255.0f) * coverage;
  if (sa >= 1.0f) {
    dst[0] = src.r;
    dst[1] = src.g;
    dst[2] = src.b;
    dst[3] = 255;
    return;
  }
  const float da = static_cast<float>(dst[3]) / 255.0f;
  const float out_a = sa + da * (1.0f - sa);
  if (out_a <= 0.0f) {
    dst[0] = dst[1] = dst[2] = dst[3] = 0;
    return;
  }
  const float sr = static_cast<float>(src.r) / 255.0f;
  const float sg = static_cast<float>(src.g) / 255.0f;
  const float sb = static_cast<float>(src.b) / 255.0f;
  const float dr = static_cast<float>(dst[0]) / 255.0f;
  const float dg = static_cast<float>(dst[1]) / 255.0f;
  const float db = static_cast<float>(dst[2]) / 255.0f;
  dst[0] = static_cast<std::uint8_t>(
      std::lround(((sr * sa + dr * da * (1.0f - sa)) / out_a) * 255.0f));
  dst[1] = static_cast<std::uint8_t>(
      std::lround(((sg * sa + dg * da * (1.0f - sa)) / out_a) * 255.0f));
  dst[2] = static_cast<std::uint8_t>(
      std::lround(((sb * sa + db * da * (1.0f - sa)) / out_a) * 255.0f));
  dst[3] = static_cast<std::uint8_t>(std::lround(out_a * 255.0f));
}

void stamp_rect_mask(std::vector<float> &mask, std::uint32_t width_px,
                     std::uint32_t height_px, const Rect2f &rect,
                     float offset_x, float offset_y) {
  DrawOp shape{.kind = DrawOpKind::fill_rect,
               .rect = rect,
               .color = {255, 255, 255, 255}};
  shape.rect.x += offset_x;
  shape.rect.y += offset_y;
  for (std::uint32_t y = 0; y < height_px; ++y) {
    for (std::uint32_t x = 0; x < width_px; ++x) {
      const float cover =
          shape_coverage(shape, static_cast<float>(x), static_cast<float>(y));
      if (cover <= 0.0f) {
        continue;
      }
      const std::size_t idx =
          static_cast<std::size_t>(y) * width_px + static_cast<std::size_t>(x);
      mask[idx] = std::max(mask[idx], cover);
    }
  }
}

void box_blur_axis(std::vector<float> &dst, const std::vector<float> &src,
                   std::uint32_t width_px, std::uint32_t height_px,
                   std::int32_t radius, bool horizontal) {
  const std::int32_t w = static_cast<std::int32_t>(width_px);
  const std::int32_t h = static_cast<std::int32_t>(height_px);
  const std::int32_t diam = radius * 2 + 1;
  if (diam <= 1) {
    dst = src;
    return;
  }
  for (std::int32_t y = 0; y < h; ++y) {
    for (std::int32_t x = 0; x < w; ++x) {
      float sum = 0.0f;
      for (std::int32_t k = -radius; k <= radius; ++k) {
        std::int32_t sx = x;
        std::int32_t sy = y;
        if (horizontal) {
          sx = std::clamp(x + k, 0, w - 1);
        } else {
          sy = std::clamp(y + k, 0, h - 1);
        }
        sum += src[static_cast<std::size_t>(sy) * width_px +
                   static_cast<std::size_t>(sx)];
      }
      dst[static_cast<std::size_t>(y) * width_px +
          static_cast<std::size_t>(x)] = sum / static_cast<float>(diam);
    }
  }
}

void separable_box_blur(std::vector<float> &mask, std::uint32_t width_px,
                        std::uint32_t height_px, float radius_px,
                        std::uint32_t iterations) {
  const float clamped = std::max(0.0f, radius_px);
  const auto radius = static_cast<std::int32_t>(std::lround(clamped));
  if (radius <= 0 || iterations == 0) {
    return;
  }
  std::vector<float> temp(mask.size(), 0.0f);
  for (std::uint32_t i = 0; i < iterations; ++i) {
    box_blur_axis(temp, mask, width_px, height_px, radius, /*horizontal=*/true);
    box_blur_axis(mask, temp, width_px, height_px, radius,
                  /*horizontal=*/false);
  }
}

void composite_mask(Rgba8Image &image, const std::vector<float> &mask,
                    Rgba8 color) {
  for (std::uint32_t y = 0; y < image.height_px; ++y) {
    for (std::uint32_t x = 0; x < image.width_px; ++x) {
      const std::size_t mi = static_cast<std::size_t>(y) * image.width_px + x;
      const float cover = mask[mi];
      if (cover <= 0.0f) {
        continue;
      }
      blend_pixel(image.rgba.data() + mi * 4U, color, cover);
    }
  }
}

[[nodiscard]] bool rect_contains(const Rect2f &rect, float x,
                                 float y) noexcept {
  return x >= rect.x && y >= rect.y && x < rect.x + rect.width &&
         y < rect.y + rect.height;
}

[[nodiscard]] Rect2f expand_rect(const Rect2f &rect, float pad) noexcept {
  return Rect2f{rect.x - pad, rect.y - pad, rect.width + 2.0f * pad,
                rect.height + 2.0f * pad};
}

struct SortedLayer {
  PaperLayerCard card{};
  std::size_t index = 0;
};

[[nodiscard]] std::vector<SortedLayer>
sort_layers_near_to_far(const std::vector<PaperLayerCard> &layers) {
  std::vector<SortedLayer> sorted;
  sorted.reserve(layers.size());
  for (std::size_t i = 0; i < layers.size(); ++i) {
    sorted.push_back(SortedLayer{.card = layers[i], .index = i});
  }
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const SortedLayer &a, const SortedLayer &b) {
                     if (a.card.depth != b.card.depth) {
                       return a.card.depth < b.card.depth;
                     }
                     return a.card.layer_order < b.card.layer_order;
                   });
  return sorted;
}

// Nearest covering layer depth per pixel; negative means background.
void build_receiver_depth_map(std::vector<float> &depth_map,
                              std::uint32_t width_px, std::uint32_t height_px,
                              const std::vector<SortedLayer> &near_to_far) {
  depth_map.assign(static_cast<std::size_t>(width_px) * height_px, -1.0f);
  for (const SortedLayer &layer : near_to_far) {
    for (std::uint32_t y = 0; y < height_px; ++y) {
      for (std::uint32_t x = 0; x < width_px; ++x) {
        if (!rect_contains(layer.card.rect, static_cast<float>(x) + 0.5f,
                           static_cast<float>(y) + 0.5f)) {
          continue;
        }
        const std::size_t idx = static_cast<std::size_t>(y) * width_px +
                                static_cast<std::size_t>(x);
        if (depth_map[idx] < 0.0f) {
          depth_map[idx] = layer.card.depth;
        }
      }
    }
  }
}

[[nodiscard]] float clamp_soft_radius(float requested, float max_spread,
                                      ShadowQualityTier quality) noexcept {
  const float tiered = shadow_blur_radius_px(requested, quality);
  const float cap =
      std::max(0.0f, std::min(max_spread,
                              std::isfinite(max_spread) ? max_spread : 0.0f));
  return std::min(tiered, cap);
}

} // namespace

std::expected<void, diagnostics::Error>
validate_paper_layered_shadow_scene(const PaperLayeredShadowScene &scene) {
  if (scene.width_px == 0 || scene.height_px == 0) {
    return std::unexpected(make_invalid(
        "invalid_extent", "layered shadow scene width/height must be > 0"));
  }
  if (scene.layers.size() < 2) {
    return std::unexpected(make_invalid(
        "too_few_layers", "layered shadows require at least two paper layers"));
  }
  const auto &p = scene.params;
  if (!std::isfinite(p.offset_x_px) || !std::isfinite(p.offset_y_px) ||
      !finite_nonneg(p.offset_per_depth) || !finite_nonneg(p.contact_depth) ||
      !finite_nonneg(p.contact_spread_px) || !finite_nonneg(p.soft_radius_px) ||
      !finite_nonneg(p.max_soft_spread_px)) {
    return std::unexpected(make_invalid(
        "invalid_params",
        "layered shadow params must be finite; distances must be >= 0"));
  }
  if (!(p.contact_depth > 0.0f)) {
    return std::unexpected(make_invalid(
        "invalid_contact_depth", "contact_depth must be finite and > 0"));
  }

  for (std::size_t i = 0; i < scene.layers.size(); ++i) {
    const PaperLayerCard &layer = scene.layers[i];
    if (!is_valid_rect(layer.rect) || !std::isfinite(layer.rect.x) ||
        !std::isfinite(layer.rect.y)) {
      return std::unexpected(
          make_invalid("invalid_layer_rect",
                       "paper layer rect must be finite and positive"));
    }
    if (!(layer.depth > 0.0f) || !std::isfinite(layer.depth)) {
      return std::unexpected(make_invalid(
          "invalid_layer_depth", "paper layer depth must be finite and > 0"));
    }
    for (std::size_t j = i + 1; j < scene.layers.size(); ++j) {
      if (scene.layers[j].depth == layer.depth) {
        return std::unexpected(make_invalid(
            "duplicate_depth",
            "paper layers must use distinct depths for shadow separation"));
      }
    }
  }
  return {};
}

[[nodiscard]] PaperLayerShadowCast
make_pair_cast(const PaperLayerCard &caster, float receiver_depth,
               const PaperLayeredShadowParams &params) {
  PaperLayerShadowCast cast{};
  cast.caster_layer_order = caster.layer_order;
  cast.caster_depth = caster.depth;
  cast.receiver_depth = receiver_depth;
  cast.depth_delta = receiver_depth - caster.depth;
  cast.contact = cast.depth_delta <= params.contact_depth;
  if (cast.contact) {
    cast.offset_x_px = params.offset_x_px * 0.35f;
    cast.offset_y_px = params.offset_y_px * 0.35f;
    cast.soft_radius_px = clamp_soft_radius(
        params.contact_spread_px, params.max_soft_spread_px, params.quality);
    cast.color = params.contact_color;
  } else {
    const float grow =
        std::min(2.25f, 1.0f + params.offset_per_depth * cast.depth_delta);
    cast.offset_x_px = params.offset_x_px * grow;
    cast.offset_y_px = params.offset_y_px * grow;
    const float requested = params.soft_radius_px *
                            std::min(1.35f, 0.85f + 0.05f * cast.depth_delta);
    cast.soft_radius_px =
        clamp_soft_radius(requested, params.max_soft_spread_px, params.quality);
    cast.color = params.shadow_color;
  }
  return cast;
}

std::expected<std::vector<PaperLayerShadowCast>, diagnostics::Error>
resolve_paper_layer_shadow_casts(const PaperLayeredShadowScene &scene) {
  if (auto ok = validate_paper_layered_shadow_scene(scene); !ok) {
    return std::unexpected(ok.error());
  }
  const auto sorted = sort_layers_near_to_far(scene.layers);
  std::vector<PaperLayerShadowCast> casts;
  casts.reserve(sorted.size());
  for (std::size_t i = 0; i < sorted.size(); ++i) {
    const PaperLayerCard &caster = sorted[i].card;
    const float receiver_depth =
        (i + 1 < sorted.size())
            ? sorted[i + 1].card.depth
            : caster.depth + std::max(scene.params.contact_depth, 1.0f);
    casts.push_back(make_pair_cast(caster, receiver_depth, scene.params));
  }
  return casts;
}

void composite_cast_mask(Rgba8Image &image, const PaperLayerCard &caster,
                         const PaperLayerShadowCast &cast,
                         const PaperLayeredShadowParams &params,
                         std::uint32_t width_px, std::uint32_t height_px,
                         const std::vector<float> &depth_map,
                         const Rect2f *receiver_rect, float receiver_depth,
                         bool background_only) {
  std::vector<float> mask(static_cast<std::size_t>(width_px) * height_px, 0.0f);
  stamp_rect_mask(mask, width_px, height_px, caster.rect, cast.offset_x_px,
                  cast.offset_y_px);
  if (cast.soft_radius_px > 0.0f) {
    separable_box_blur(mask, width_px, height_px, cast.soft_radius_px,
                       shadow_blur_iterations(params.quality));
  }
  const Rect2f clip =
      receiver_rect
          ? expand_rect(*receiver_rect, params.max_soft_spread_px + 1.0f)
          : Rect2f{0, 0, static_cast<float>(width_px),
                   static_cast<float>(height_px)};
  for (std::uint32_t y = 0; y < height_px; ++y) {
    for (std::uint32_t x = 0; x < width_px; ++x) {
      const std::size_t idx = static_cast<std::size_t>(y) * width_px + x;
      if (mask[idx] <= 0.0f) {
        continue;
      }
      const float px = static_cast<float>(x) + 0.5f;
      const float py = static_cast<float>(y) + 0.5f;
      if (!rect_contains(clip, px, py)) {
        mask[idx] = 0.0f;
        continue;
      }
      const float recv = depth_map[idx];
      if (background_only) {
        if (recv >= 0.0f) {
          mask[idx] = 0.0f;
        }
        continue;
      }
      if (receiver_rect && !rect_contains(*receiver_rect, px, py)) {
        mask[idx] = 0.0f;
        continue;
      }
      // Keep the soft edge on the receiver sheet only (no floating halo).
      if (std::fabs(recv - receiver_depth) > 1e-3f ||
          !(receiver_depth > caster.depth)) {
        mask[idx] = 0.0f;
      }
    }
  }
  composite_mask(image, mask, cast.color);
}

std::expected<Rgba8Image, diagnostics::Error>
rasterize_paper_layered_shadows(const PaperLayeredShadowScene &scene) {
  if (auto ok = validate_paper_layered_shadow_scene(scene); !ok) {
    return std::unexpected(ok.error());
  }

  Rgba8Image image;
  image.width_px = scene.width_px;
  image.height_px = scene.height_px;
  image.rgba.assign(static_cast<std::size_t>(scene.width_px) *
                        static_cast<std::size_t>(scene.height_px) * 4U,
                    0);
  DrawOp clear{.kind = DrawOpKind::clear, .color = scene.clear_color};
  if (auto cleared = apply_draw_op(image, clear); !cleared) {
    return std::unexpected(cleared.error());
  }

  const auto sorted = sort_layers_near_to_far(scene.layers);
  std::vector<float> depth_map;
  build_receiver_depth_map(depth_map, scene.width_px, scene.height_px, sorted);

  // Stage-floor drops on uncovered background (visible depth under the stack).
  for (const SortedLayer &layer : sorted) {
    const float ground =
        layer.card.depth + std::max(scene.params.contact_depth, 1.0f);
    const auto cast = make_pair_cast(layer.card, ground, scene.params);
    composite_cast_mask(image, layer.card, cast, scene.params, scene.width_px,
                        scene.height_px, depth_map, /*receiver_rect=*/nullptr,
                        /*receiver_depth=*/-1.0f, /*background_only=*/true);
  }

  // Far → near: paint sheet, then nearer casters darken that sheet surface.
  for (std::size_t recv = sorted.size(); recv-- > 0;) {
    const PaperLayerCard &receiver = sorted[recv].card;
    DrawOp fill{.kind = DrawOpKind::fill_rect,
                .rect = receiver.rect,
                .color = receiver.fill};
    if (auto drawn = apply_draw_op(image, fill); !drawn) {
      return std::unexpected(drawn.error());
    }
    for (std::size_t c = 0; c < recv; ++c) {
      const auto cast =
          make_pair_cast(sorted[c].card, receiver.depth, scene.params);
      composite_cast_mask(image, sorted[c].card, cast, scene.params,
                          scene.width_px, scene.height_px, depth_map,
                          &receiver.rect, receiver.depth,
                          /*background_only=*/false);
    }
  }
  return image;
}

std::string
format_paper_layered_shadow_golden(std::string_view label,
                                   const PaperLayeredShadowScene &scene,
                                   const Rgba8Image &image) {
  std::ostringstream ss;
  ss << "name=" << label << " layers=" << scene.layers.size()
     << " w=" << scene.width_px << " h=" << scene.height_px
     << " offset=" << scene.params.offset_x_px << ","
     << scene.params.offset_y_px
     << " contact_depth=" << scene.params.contact_depth
     << " soft=" << scene.params.soft_radius_px
     << " max_spread=" << scene.params.max_soft_spread_px << '\n';

  if (auto casts = resolve_paper_layer_shadow_casts(scene)) {
    for (const PaperLayerShadowCast &c : *casts) {
      ss << "cast order=" << c.caster_layer_order << " depth=" << c.caster_depth
         << " recv=" << c.receiver_depth << " delta=" << c.depth_delta
         << " contact=" << (c.contact ? 1 : 0) << " off=" << c.offset_x_px
         << "," << c.offset_y_px << " soft=" << c.soft_radius_px << '\n';
    }
  }

  const std::uint32_t samples[][2] = {{2, 2},   {8, 8},   {12, 14},
                                      {18, 10}, {22, 20}, {28, 28}};
  for (const auto &s : samples) {
    if (s[0] >= image.width_px || s[1] >= image.height_px) {
      continue;
    }
    const std::size_t idx =
        (static_cast<std::size_t>(s[1]) * image.width_px + s[0]) * 4U;
    ss << "p" << s[0] << "," << s[1] << "=" << static_cast<int>(image.rgba[idx])
       << "," << static_cast<int>(image.rgba[idx + 1]) << ","
       << static_cast<int>(image.rgba[idx + 2]) << "\n";
  }
  return ss.str();
}

} // namespace instantee::render_core
