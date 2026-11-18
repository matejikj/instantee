#include <instantee/render_core/paper_sheet.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace instantee::render_core {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "paper_sheet", message};
}

[[nodiscard]] std::uint32_t mix_u32(std::uint32_t a, std::uint32_t b) noexcept {
  std::uint32_t x = a * 1664525u + b + 1013904223u;
  x ^= x >> 16;
  x *= 2246822519u;
  x ^= x >> 13;
  return x;
}

[[nodiscard]] float unit_from_hash(std::uint32_t h) noexcept {
  return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

[[nodiscard]] float clamp01(float v) noexcept {
  return std::min(1.0f, std::max(0.0f, v));
}

[[nodiscard]] std::uint8_t to_u8(float v) noexcept {
  return static_cast<std::uint8_t>(std::lround(clamp01(v) * 255.0f));
}

[[nodiscard]] float channel01(std::uint8_t c) noexcept {
  return static_cast<float>(c) / 255.0f;
}

} // namespace

float paper_sheet_unit_noise(std::uint32_t seed, std::uint32_t x,
                             std::uint32_t y, std::uint32_t channel) noexcept {
  return unit_from_hash(
      mix_u32(seed, mix_u32(x, mix_u32(y, channel ^ 0xA5A5A5A5u))));
}

std::expected<void, diagnostics::Error>
validate_paper_sheet_shading_params(const PaperSheetShadingParams &params) {
  if (auto ok = validate_paper_material_params(params.material); !ok) {
    return std::unexpected(ok.error());
  }
  return {};
}

ShaderFeatureRegistry make_paper_sheet_shader_feature_registry() {
  // Reuse the note-card registry so paper-sheet stays a single registered
  // feature across notebook and Paper Cinema consumers.
  return make_paper_note_shader_feature_registry();
}

std::expected<PaperSheetShadingPassPlan, diagnostics::Error>
plan_paper_sheet_shading_passes(
    const ShaderFeatureRegistry &registry, const MaterialDesc &material,
    const std::vector<std::string> &active_feature_names) {
  auto planned = registry.plan_passes(material, active_feature_names);
  if (!planned) {
    return std::unexpected(planned.error());
  }
  PaperSheetShadingPassPlan out;
  out.pass_keys = planned->pass_keys;
  out.paper_sheet_active =
      std::find(out.pass_keys.begin(), out.pass_keys.end(),
                std::string{kPaperSheetPass}) != out.pass_keys.end();
  return out;
}

std::expected<Rgba8Image, diagnostics::Error>
rasterize_paper_sheet_shading_reference(std::uint32_t width_px,
                                        std::uint32_t height_px,
                                        const PaperSheetShadingParams &params) {
  if (width_px == 0 || height_px == 0) {
    return std::unexpected(make_invalid(
        "invalid_extent", "paper-sheet reference requires positive extents"));
  }
  if (auto ok = validate_paper_sheet_shading_params(params); !ok) {
    return std::unexpected(ok.error());
  }

  const auto &m = params.material;
  Rgba8Image image;
  image.width_px = width_px;
  image.height_px = height_px;
  image.rgba.assign(static_cast<std::size_t>(width_px) * height_px * 4U, 0);

  const float w = static_cast<float>(width_px);
  const float h = static_cast<float>(height_px);

  for (std::uint32_t y = 0; y < height_px; ++y) {
    for (std::uint32_t x = 0; x < width_px; ++x) {
      const float fx = static_cast<float>(x) + 0.5f;
      const float fy = static_cast<float>(y) + 0.5f;
      const float nx = (fx / w) * 2.0f - 1.0f;
      const float ny = (fy / h) * 2.0f - 1.0f;

      const float falloff = clamp01(1.0f - 0.12f * (nx * nx + ny * ny));
      float r = channel01(params.pulp_color.r) * falloff;
      float g = channel01(params.pulp_color.g) * falloff;
      float b = channel01(params.pulp_color.b) * falloff;

      const float fiber_a = paper_sheet_unit_noise(m.seed, x, y / 2u, 1u);
      const float fiber_b = paper_sheet_unit_noise(m.seed, x / 3u, y, 2u);
      const float fiber_mix = fiber_a * 0.65f + fiber_b * 0.35f;
      const float fiber_scale = 1.0f + (fiber_mix - 0.5f) * 0.55f * m.fiber;
      r *= fiber_scale;
      g *= fiber_scale;
      b *= fiber_scale;

      const float rough = paper_sheet_unit_noise(m.seed, x, y, 3u);
      const float rough_scale = 1.0f + (rough - 0.5f) * 0.35f * m.roughness;
      r *= rough_scale;
      g *= rough_scale;
      b *= rough_scale;

      const float wash_mask = clamp01(
          0.35f + 0.65f * paper_sheet_unit_noise(m.seed, x / 2u, y / 2u, 4u));
      const float wash_t = wash_mask * m.color_wash;
      r = r * (1.0f - wash_t) + channel01(params.wash_color.r) * wash_t;
      g = g * (1.0f - wash_t) + channel01(params.wash_color.g) * wash_t;
      b = b * (1.0f - wash_t) + channel01(params.wash_color.b) * wash_t;

      const float nx_n = (paper_sheet_unit_noise(m.seed, x, y, 5u) - 0.5f) *
                         2.0f * m.normal_variation;
      const float ny_n = (paper_sheet_unit_noise(m.seed, x, y, 6u) - 0.5f) *
                         2.0f * m.normal_variation;
      const float n_len = std::sqrt(nx_n * nx_n + ny_n * ny_n + 1.0f);
      const float nnx = nx_n / n_len;
      const float nny = ny_n / n_len;
      const float nnz = 1.0f / n_len;
      constexpr float lx = -0.25f;
      constexpr float ly = -0.4f;
      constexpr float lz = 1.0f;
      const float l_len = std::sqrt(lx * lx + ly * ly + lz * lz);
      const float lambert = clamp01((nnx * lx + nny * ly + nnz * lz) / l_len);
      const float shade = 0.88f + 0.20f * lambert;
      r *= shade;
      g *= shade;
      b *= shade;

      const float edge_x = std::min(fx, w - fx) / w;
      const float edge_y = std::min(fy, h - fy) / h;
      const float edge = clamp01(std::min(edge_x, edge_y) /
                                 std::max(m.edge_thickness, 0.001f));
      const float rim = (1.0f - edge) * (0.25f + 0.75f * m.shadow_bias);
      const float rim_scale = 1.0f - 0.22f * rim;
      r *= rim_scale;
      g *= rim_scale;
      b *= rim_scale;

      const std::size_t idx = (static_cast<std::size_t>(y) * width_px + x) * 4U;
      image.rgba[idx] = to_u8(r);
      image.rgba[idx + 1] = to_u8(g);
      image.rgba[idx + 2] = to_u8(b);
      image.rgba[idx + 3] = params.pulp_color.a;
    }
  }
  return image;
}

} // namespace instantee::render_core
