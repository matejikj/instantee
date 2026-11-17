#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/geometry/path.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace instantee::asset_pipeline {

struct SvgRgba8 {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;
};

struct SvgImportLimits {
  std::size_t max_input_bytes = 4U * 1024U * 1024U;
  std::uint32_t max_paths = 4096;
  std::uint32_t max_commands = 256U * 1024U;
  // Reject documents declaring a viewBox/canvas larger than this.
  float max_extent = 16384.0f;
};

struct SvgPathItem {
  geometry::Path2f path{};
  bool has_fill = false;
  geometry::FillRule fill_rule = geometry::FillRule::nonzero;
  SvgRgba8 fill_color{};
  bool has_stroke = false;
  float stroke_width = 1.0f;
  SvgRgba8 stroke_color{};
};

struct ImportedSvg {
  float width = 0;
  float height = 0;
  std::vector<SvgPathItem> paths{};
};

// Security pre-scan only: rejects scripts, event handlers, remote URL
// references and a set of non-subset host elements. Does not parse geometry.
[[nodiscard]] std::expected<void, diagnostics::Error>
validate_svg_security(std::span<const std::uint8_t> bytes,
                      const SvgImportLimits &limits = {});

// Validate security, then parse a static subset into normalized Path2f items.
// External references are never fetched. Unsupported/complex constructs fail
// closed with an explicit error (no silent network or script fallback).
[[nodiscard]] std::expected<ImportedSvg, diagnostics::Error>
import_svg(std::span<const std::uint8_t> bytes,
           const SvgImportLimits &limits = {});

} // namespace instantee::asset_pipeline
