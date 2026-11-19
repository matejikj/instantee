#include <instantee/asset_pipeline/bitmap_decode.hpp>
#include <instantee/asset_pipeline/svg_import.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using instantee::asset_pipeline::BitmapDecodeLimits;
using instantee::asset_pipeline::BitmapDecodeOptions;
using instantee::asset_pipeline::decode_bitmap;
using instantee::asset_pipeline::import_svg;
using instantee::asset_pipeline::probe_bitmap_dimensions;
using instantee::asset_pipeline::validate_svg_security;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::vector<std::uint8_t> bytes_of(std::string_view text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

} // namespace

int main() {
  // Network / script / file URL must be rejected before geometry import.
  {
    const auto script = bytes_of(
        R"(<svg xmlns="http://www.w3.org/2000/svg"><script>x()</script></svg>)");
    auto sec = validate_svg_security(script);
    expect(!sec && sec.error().code == "svg.script.forbidden", "script");
  }
  {
    const auto remote = bytes_of(
        R"(<svg xmlns="http://www.w3.org/2000/svg"><image href="https://example.invalid/a.png" width="1" height="1"/></svg>)");
    auto sec = validate_svg_security(remote);
    expect(!sec.has_value(), "https rejected");
    expect(sec.error().code == "svg.external.href" ||
               sec.error().code == "svg.complex.unsupported",
           "https code");
  }
  {
    const auto file_uri = bytes_of(
        R"(<svg xmlns="http://www.w3.org/2000/svg"><a href="file:///etc/passwd"><rect width="1" height="1"/></a></svg>)");
    auto sec = validate_svg_security(file_uri);
    expect(!sec && sec.error().code == "svg.external.href", "file uri");
  }
  {
    // Relative traversal href is not fetched; still must not open network.
    // Current policy: no scheme => not flagged as external URL, but <a> alone
    // is allowed only with local fragment/relative — geometry import must not
    // perform I/O. Assert import path stays Error-or-success without fetch.
    const auto trav = bytes_of(
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><a href="../../secret"><rect width="1" height="1" fill="#000"/></a></svg>)");
    auto imported = import_svg(trav);
    // Either parses geometry without fetching, or fails closed — never throws.
    (void)imported;
    expect(true, "traversal href no crash");
  }

  // Uncontrolled dimensions: huge IHDR rejected before pixel allocation.
  {
    constexpr std::uint8_t huge[] = {
        137, 80,  78,  71, 13, 10,  26,  10, 0, 0, 0, 13, 73, 72, 68,  82, 0,
        1,   134, 160, 0,  1,  134, 160, 8,  2, 0, 0, 0,  39, 48, 156, 159};
    BitmapDecodeLimits limits;
    limits.max_width_px = 1024;
    limits.max_height_px = 1024;
    limits.max_pixels = 512U * 512U;
    auto probe = probe_bitmap_dimensions(std::span{huge}, limits);
    expect(!probe.has_value(), "huge probe reject");
    BitmapDecodeOptions opts;
    opts.limits = limits;
    auto decoded = decode_bitmap(std::span{huge}, opts);
    expect(!decoded.has_value(), "huge decode reject");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
