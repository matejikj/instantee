#include <instantee/asset_pipeline/svg_import.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#if defined(NANOSVG_H) || defined(NANOSVG_IMPLEMENTATION)
#error "asset-pipeline public tests must not pull nanosvg headers"
#endif

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::span<const std::uint8_t> bytes_of(std::string_view text) {
  return {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()};
}

void expect_reject(std::string_view svg, std::string_view expected_code,
                   const char *label) {
  using namespace instantee::asset_pipeline;
  const auto result = import_svg(bytes_of(svg));
  expect(!result.has_value(), label);
  if (!result.has_value()) {
    expect(result.error().code == expected_code, label);
  }
}

} // namespace

int main() {
  using namespace instantee::asset_pipeline;
  using namespace instantee::geometry;
  using namespace instantee::diagnostics;

  expect_reject(
      R"SVG(<svg xmlns="http://www.w3.org/2000/svg"><script>alert(1)</script></svg>)SVG",
      "svg.script.forbidden", "reject script");
  expect_reject(
      R"SVG(<svg xmlns="http://www.w3.org/2000/svg"><rect width="10" height="10" onclick="x()"/></svg>)SVG",
      "svg.script.forbidden", "reject onclick");
  expect_reject(
      R"SVG(<svg xmlns="http://www.w3.org/2000/svg"><a xlink:href="javascript:alert(1)"><rect width="1" height="1"/></a></svg>)SVG",
      "svg.script.forbidden", "reject javascript href");

  expect_reject(
      R"SVG(<svg xmlns="http://www.w3.org/2000/svg"><image href="https://example.com/x.png" width="10" height="10"/></svg>)SVG",
      "svg.external.href", "reject https image");
  expect_reject(
      R"SVG(<svg xmlns="http://www.w3.org/2000/svg"><rect width="1" height="1" fill="url(https://evil/x)"/></svg>)SVG",
      "svg.external.style", "reject remote css url");
  expect_reject(
      R"SVG(<svg xmlns="http://www.w3.org/2000/svg"><a href="//cdn.example/sprite.svg#icon"><rect width="1" height="1"/></a></svg>)SVG",
      "svg.external.href", "reject protocol-relative href");
  expect_reject(
      R"SVG(<svg xmlns="http://www.w3.org/2000/svg"><a href="data:image/png;base64,aaa"><rect width="1" height="1"/></a></svg>)SVG",
      "svg.external.href", "reject data href");

  expect_reject(
      R"SVG(<svg xmlns="http://www.w3.org/2000/svg"><image href="local.png" width="10" height="10"/></svg>)SVG",
      "svg.complex.unsupported", "reject image element");
  expect_reject(
      R"SVG(<svg xmlns="http://www.w3.org/2000/svg"><defs><filter id="f"><feGaussianBlur/></filter></defs><rect filter="url(#f)" width="1" height="1"/></svg>)SVG",
      "svg.complex.unsupported", "reject filter");
  expect_reject(
      R"SVG(<svg xmlns="http://www.w3.org/2000/svg"><mask id="m"><rect width="1" height="1"/></mask><rect mask="url(#m)" width="1" height="1"/></svg>)SVG",
      "svg.complex.unsupported", "reject mask");
  expect_reject(
      R"SVG(<svg xmlns="http://www.w3.org/2000/svg"><defs><path id="p" d="M0 0 L1 0"/></defs><use href="#p"/></svg>)SVG",
      "svg.complex.unsupported", "reject use");

  {
    SvgImportLimits limits{};
    limits.max_input_bytes = 32;
    const auto result = import_svg(
        bytes_of(
            R"SVG(<svg xmlns="http://www.w3.org/2000/svg"><rect width="10" height="10"/></svg>)SVG"),
        limits);
    expect(!result.has_value(), "size budget");
    if (!result.has_value()) {
      expect(result.error().kind == ErrorKind::resource_budget_exceeded,
             "size budget kind");
      expect(result.error().code == "svg.input.limit", "size budget code");
    }
  }

  {
    constexpr std::string_view kSvg =
        R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="50"><rect x="10" y="20" width="30" height="40" fill="#ff0000"/></svg>)SVG";
    const auto result = import_svg(bytes_of(kSvg));
    expect(result.has_value(), "rect import");
    if (result) {
      expect(result->paths.size() == 1, "rect path count");
      const auto &item = result->paths.front();
      expect(item.has_fill, "rect has fill");
      expect(item.fill_color.r == 255 && item.fill_color.g == 0 &&
                 item.fill_color.b == 0,
             "rect red fill");
      expect(!item.has_stroke, "rect no stroke");
      expect(!item.path.commands.empty(), "rect commands");
      expect(item.path.commands.front().verb == PathVerb::move_to,
             "rect starts with move");
      bool closed = false;
      for (const auto &cmd : item.path.commands) {
        if (cmd.verb == PathVerb::close) {
          closed = true;
        }
      }
      expect(closed, "rect closed");
    }
  }

  {
    constexpr std::string_view kSvg =
        R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><path d="M10 10 L90 10 L90 90 Z" fill="none" stroke="#00ff00" stroke-width="2"/></svg>)SVG";
    const auto result = import_svg(bytes_of(kSvg));
    expect(result.has_value(), "path import");
    if (result) {
      expect(result->paths.size() == 1, "path count");
      const auto &item = result->paths.front();
      expect(!item.has_fill, "path no fill");
      expect(item.has_stroke, "path has stroke");
      expect(item.stroke_color.r == 0 && item.stroke_color.g == 255 &&
                 item.stroke_color.b == 0,
             "path green stroke");
      expect(item.stroke_width > 1.5f && item.stroke_width < 2.5f,
             "path stroke width");
      expect(item.path.commands.size() >= 3, "path command count");
      expect(item.path.commands.front().verb == PathVerb::move_to, "path move");
      expect(item.path.commands.front().p0.x > 9.0f &&
                 item.path.commands.front().p0.x < 11.0f &&
                 item.path.commands.front().p0.y > 9.0f &&
                 item.path.commands.front().p0.y < 11.0f,
             "path start point");
    }
  }

  {
    const auto empty = import_svg(bytes_of(""));
    expect(!empty.has_value(), "empty rejected");
    if (!empty.has_value()) {
      expect(empty.error().code == "svg.empty", "empty code");
    }
    expect_reject(R"SVG(<svg xmlns="http://www.w3.org/2000/svg"></svg>)SVG",
                  "svg.empty.geometry", "empty geometry");
  }

  // Reproducibility: identical input yields identical command verbs and colors.
  {
    constexpr std::string_view kSvg =
        R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="40" height="40"><circle cx="20" cy="20" r="10" fill="#112233"/></svg>)SVG";
    const auto a = import_svg(bytes_of(kSvg));
    const auto b = import_svg(bytes_of(kSvg));
    expect(a.has_value() && b.has_value(), "repro import");
    if (a && b) {
      expect(a->paths.size() == b->paths.size(), "repro path count");
      if (!a->paths.empty() && a->paths.size() == b->paths.size()) {
        expect(a->paths[0].fill_color.r == b->paths[0].fill_color.r &&
                   a->paths[0].fill_color.g == b->paths[0].fill_color.g &&
                   a->paths[0].fill_color.b == b->paths[0].fill_color.b,
               "repro color");
        expect(a->paths[0].path.commands.size() ==
                   b->paths[0].path.commands.size(),
               "repro commands");
      }
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
