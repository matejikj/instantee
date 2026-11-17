#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/render_graph.hpp>
#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/frame_evaluator.hpp>
#include <instantee/runtime/frame_preview.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace instantee::preview {

// Preview PNG color space. Matches Scene IR canvas "rec709-sdr": display-
// referred Rec.709 / IEC 61966-2-1 transfer encoded as straight RGBA8.
enum class PreviewColorSpace : std::uint8_t {
  rec709_sdr = 1,
};

struct FramePngRequest {
  PreviewColorSpace color_space = PreviewColorSpace::rec709_sdr;
  // Optional clear; default is opaque black in the chosen color space.
  std::optional<render_core::Rgba8> clear_color{};
};

// Single-eval random-access raster of film frame `at`. Never walks 0..at-1.
// Drawable nodes become axis-aligned proxy fills (text uses font metrics
// extent; geometry uses a unit local square scaled by the world matrix).
[[nodiscard]] std::expected<render_core::Rgba8Image, diagnostics::Error>
rasterize_preview_frame(const runtime::CompiledScene &scene,
                        const runtime::FrameState &state,
                        const FramePngRequest &request = {});

// Writes an RGBA8 PNG. Embeds an sRGB intent chunk for rec709_sdr previews
// (SDR transfer shared with Rec.709 encoding used by the compositor).
[[nodiscard]] std::expected<void, diagnostics::Error>
write_rgba8_png(const std::filesystem::path &path, std::uint32_t width_px,
                std::uint32_t height_px, std::span<const std::uint8_t> rgba,
                PreviewColorSpace color_space = PreviewColorSpace::rec709_sdr);

// preview_frame_at → rasterize → PNG. Rejects non-rec709 canvas labels.
[[nodiscard]] std::expected<runtime::FramePreview, diagnostics::Error>
export_preview_png(const runtime::CompiledScene &scene,
                   const runtime::AnimationProgram &program,
                   core::FrameIndex at, const std::filesystem::path &png_path,
                   const FramePngRequest &request = {});

[[nodiscard]] constexpr std::string_view
preview_color_space_id(PreviewColorSpace space) noexcept {
  switch (space) {
  case PreviewColorSpace::rec709_sdr:
    return "rec709-sdr";
  }
  return "unknown";
}

} // namespace instantee::preview
