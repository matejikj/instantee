#include <instantee/asset_pipeline/svg_import.hpp>

#include <nanosvg.h>

#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::asset_pipeline {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "asset_pipeline", message};
}

[[nodiscard]] diagnostics::Error make_budget(std::string_view code,
                                             std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::resource_budget_exceeded,
                            diagnostics::ErrorKind::resource_budget_exceeded,
                            code, "asset_pipeline", message};
}

[[nodiscard]] SvgRgba8 from_nsvg_color(unsigned int color) noexcept {
  // NanoSVG packs colors as R | (G<<8) | (B<<16) | (A<<24).
  return SvgRgba8{
      .r = static_cast<std::uint8_t>((color >> 0) & 0xffU),
      .g = static_cast<std::uint8_t>((color >> 8) & 0xffU),
      .b = static_cast<std::uint8_t>((color >> 16) & 0xffU),
      .a = static_cast<std::uint8_t>((color >> 24) & 0xffU),
  };
}

void append_path_from_nsvg(geometry::Path2f &out, const NSVGpath *path) {
  if (path == nullptr || path->npts < 1) {
    return;
  }
  const float *pts = path->pts;
  out.commands.push_back(geometry::PathCommand{
      .verb = geometry::PathVerb::move_to,
      .p0 = {pts[0], pts[1]},
  });
  for (int i = 0; i < path->npts - 1; i += 3) {
    const float *p = &pts[i * 2];
    out.commands.push_back(geometry::PathCommand{
        .verb = geometry::PathVerb::cubic_to,
        .p0 = {p[6], p[7]},
        .p1 = {p[2], p[3]},
        .p2 = {p[4], p[5]},
    });
  }
  if (path->closed) {
    out.commands.push_back(
        geometry::PathCommand{.verb = geometry::PathVerb::close});
  }
}

} // namespace

std::expected<ImportedSvg, diagnostics::Error>
import_svg(std::span<const std::uint8_t> bytes, const SvgImportLimits &limits) {
  auto secure = validate_svg_security(bytes, limits);
  if (!secure) {
    return std::unexpected(secure.error());
  }

  // NanoSVG mutates the input buffer in place; keep a NUL-terminated copy.
  std::string buffer(reinterpret_cast<const char *>(bytes.data()),
                     bytes.size());
  buffer.push_back('\0');

  NSVGimage *image = nsvgParse(buffer.data(), "px", 96.0f);
  if (image == nullptr) {
    return std::unexpected(
        make_invalid("svg.parse.failed", "SVG subset parse failed"));
  }

  struct ImageGuard {
    NSVGimage *ptr;
    ~ImageGuard() {
      if (ptr != nullptr) {
        nsvgDelete(ptr);
      }
    }
  } guard{image};

  if (!std::isfinite(image->width) || !std::isfinite(image->height) ||
      image->width < 0.0f || image->height < 0.0f ||
      image->width > limits.max_extent || image->height > limits.max_extent) {
    return std::unexpected(
        make_budget("svg.extent.limit", "SVG canvas extent exceeds limit"));
  }

  ImportedSvg out;
  out.width = image->width;
  out.height = image->height;

  std::uint32_t command_count = 0;
  for (NSVGshape *shape = image->shapes; shape != nullptr;
       shape = shape->next) {
    if ((shape->flags & NSVG_FLAGS_VISIBLE) == 0) {
      continue;
    }
    if (out.paths.size() >= limits.max_paths) {
      return std::unexpected(
          make_budget("svg.paths.limit", "SVG path count exceeds limit"));
    }

    SvgPathItem item;
    item.has_fill = shape->fill.type != NSVG_PAINT_NONE;
    item.fill_rule = (shape->fillRule == NSVG_FILLRULE_EVENODD)
                         ? geometry::FillRule::even_odd
                         : geometry::FillRule::nonzero;
    if (shape->fill.type == NSVG_PAINT_COLOR) {
      item.fill_color = from_nsvg_color(shape->fill.color);
    } else if (shape->fill.type == NSVG_PAINT_NONE) {
      // leave default
    } else {
      // Gradients / patterns are outside the static solid subset.
      return std::unexpected(make_invalid(
          "svg.complex.paint",
          "SVG fill/stroke paint is not a solid color (unsupported)"));
    }

    item.has_stroke = shape->stroke.type != NSVG_PAINT_NONE;
    item.stroke_width = shape->strokeWidth;
    if (shape->stroke.type == NSVG_PAINT_COLOR) {
      item.stroke_color = from_nsvg_color(shape->stroke.color);
    } else if (shape->stroke.type != NSVG_PAINT_NONE) {
      return std::unexpected(make_invalid(
          "svg.complex.paint",
          "SVG fill/stroke paint is not a solid color (unsupported)"));
    }

    for (NSVGpath *path = shape->paths; path != nullptr; path = path->next) {
      append_path_from_nsvg(item.path, path);
    }
    command_count += static_cast<std::uint32_t>(item.path.commands.size());
    if (command_count > limits.max_commands) {
      return std::unexpected(
          make_budget("svg.commands.limit", "SVG command count exceeds limit"));
    }
    if (!item.path.commands.empty() && (item.has_fill || item.has_stroke)) {
      out.paths.push_back(std::move(item));
    }
  }

  if (out.paths.empty()) {
    return std::unexpected(make_invalid(
        "svg.empty.geometry", "SVG produced no importable static paths"));
  }
  return out;
}

} // namespace instantee::asset_pipeline
