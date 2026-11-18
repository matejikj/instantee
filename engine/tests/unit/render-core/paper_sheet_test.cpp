#include <instantee/render_core/paper_sheet.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__)
#error "render-core tests must remain free of Metal/Apple headers"
#endif

namespace {

using namespace instantee::render_core;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] Rgba8 at(const Rgba8Image &img, std::uint32_t x,
                       std::uint32_t y) {
  const std::size_t idx = (static_cast<std::size_t>(y) * img.width_px + x) * 4U;
  return Rgba8{img.rgba[idx], img.rgba[idx + 1], img.rgba[idx + 2],
               img.rgba[idx + 3]};
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

[[nodiscard]] bool write_if_missing(const std::filesystem::path &path,
                                    std::string_view body) {
  if (std::filesystem::exists(path)) {
    return true;
  }
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out << body;
  return static_cast<bool>(out);
}

[[nodiscard]] double mean_luma(const Rgba8Image &img) {
  if (img.rgba.empty()) {
    return 0.0;
  }
  double sum = 0.0;
  std::size_t n = 0;
  for (std::size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
    sum += 0.2126 * img.rgba[i] + 0.7152 * img.rgba[i + 1] +
           0.0722 * img.rgba[i + 2];
    ++n;
  }
  return n == 0 ? 0.0 : sum / static_cast<double>(n);
}

[[nodiscard]] std::string golden_dump(std::string_view name,
                                      const PaperSheetShadingParams &params,
                                      const Rgba8Image &img) {
  std::ostringstream ss;
  ss << "name=" << name << " seed=" << params.material.seed
     << " fiber=" << params.material.fiber
     << " roughness=" << params.material.roughness
     << " wash=" << params.material.color_wash
     << " edge=" << params.material.edge_thickness
     << " shadow=" << params.material.shadow_bias
     << " nvar=" << params.material.normal_variation
     << " mean_luma_q=" << static_cast<int>(std::lround(mean_luma(img)))
     << '\n';
  const std::uint32_t samples[][2] = {
      {0, 0}, {4, 3}, {8, 6}, {12, 9}, {15, 11}};
  for (const auto &s : samples) {
    const Rgba8 c = at(img, s[0], s[1]);
    ss << "p" << s[0] << "," << s[1] << "=" << static_cast<int>(c.r) << ","
       << static_cast<int>(c.g) << "," << static_cast<int>(c.b) << "\n";
  }
  return ss.str();
}

[[nodiscard]] double mean_abs_delta_u8(const Rgba8Image &a,
                                       const Rgba8Image &b) {
  const std::size_t n = std::min(a.rgba.size(), b.rgba.size());
  if (n == 0) {
    return 0.0;
  }
  double sum = 0.0;
  std::size_t count = 0;
  for (std::size_t i = 0; i + 3 < n; i += 4) {
    sum += std::abs(static_cast<int>(a.rgba[i]) - static_cast<int>(b.rgba[i]));
    sum += std::abs(static_cast<int>(a.rgba[i + 1]) -
                    static_cast<int>(b.rgba[i + 1]));
    sum += std::abs(static_cast<int>(a.rgba[i + 2]) -
                    static_cast<int>(b.rgba[i + 2]));
    count += 3;
  }
  return count == 0 ? 0.0 : sum / static_cast<double>(count);
}

// Nearest-neighbor upsample of a small reference to compare scale stability.
[[nodiscard]] Rgba8Image upsample_nn(const Rgba8Image &src,
                                     std::uint32_t scale) {
  Rgba8Image out;
  out.width_px = src.width_px * scale;
  out.height_px = src.height_px * scale;
  out.rgba.assign(static_cast<std::size_t>(out.width_px) * out.height_px * 4U,
                  0);
  for (std::uint32_t y = 0; y < out.height_px; ++y) {
    for (std::uint32_t x = 0; x < out.width_px; ++x) {
      const Rgba8 c = at(src, x / scale, y / scale);
      const std::size_t idx =
          (static_cast<std::size_t>(y) * out.width_px + x) * 4U;
      out.rgba[idx] = c.r;
      out.rgba[idx + 1] = c.g;
      out.rgba[idx + 2] = c.b;
      out.rgba[idx + 3] = c.a;
    }
  }
  return out;
}

} // namespace

int main(int argc, char **argv) {
  const bool write_goldens =
      argc == 3 && std::string_view(argv[2]) == "--write-goldens";
  if (argc != 2 && !write_goldens) {
    std::cerr << "usage: paper_sheet_test <repo-root> [--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  const auto golden_dir = root / "contracts/fixtures/paper-sheet/goldens";
  const auto profile_path =
      root / "contracts/fixtures/paper-sheet/paper-sheet.profile.json";
  expect(std::filesystem::exists(profile_path), "profile present");

  auto registry = make_paper_sheet_shader_feature_registry();
  expect(registry.contains(kPaperSheetFeature), "paper-sheet registered");
  expect(!registry.contains("paper-cinema"), "no style package branch");

  auto feature = registry.resolve(kPaperSheetFeature);
  expect(feature.has_value(), "resolve paper-sheet");
  if (feature) {
    expect(!feature->params.empty(), "paper-sheet params");
    expect(feature->pass_contributions.size() == 1 &&
               feature->pass_contributions[0].pass_key == kPaperSheetPass,
           "paper-sheet pass");
  }

  MaterialDesc material{.id = "paper_panel",
                        .version = 1,
                        .feature_names = {std::string{kPaperSheetFeature}}};

  auto inactive = plan_paper_sheet_shading_passes(registry, material,
                                                  std::vector<std::string>{});
  expect(inactive.has_value() && !inactive->paper_sheet_active &&
             inactive->pass_keys.empty(),
         "inactive empty pass");

  auto active = plan_paper_sheet_shading_passes(
      registry, material,
      std::vector<std::string>{std::string{kPaperSheetFeature}});
  expect(active.has_value() && active->paper_sheet_active, "active plan");
  if (active) {
    expect(active->pass_keys.size() == 1, "one paper-sheet pass");
    expect(active->pass_keys[0] == kPaperSheetPass, "paper-sheet pass key");
  }

  PaperSheetShadingParams params;
  params.material.seed = 17;
  params.material.fiber = 0.55f;
  params.material.roughness = 0.4f;
  params.material.color_wash = 0.18f;
  params.material.edge_thickness = 0.08f;
  params.material.shadow_bias = 0.2f;
  params.material.normal_variation = 0.22f;

  auto img = rasterize_paper_sheet_shading_reference(16, 12, params);
  expect(img.has_value(), "raster paper-sheet");
  if (img) {
    expect(mean_luma(*img) > 150.0, "paper stays light");
    const std::string golden = golden_dump("matte-wash", params, *img);
    const auto path = golden_dir / "matte-wash.txt";
    if (write_goldens) {
      std::filesystem::create_directories(path.parent_path());
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      out << golden;
      expect(static_cast<bool>(out), "write matte-wash golden");
    } else {
      expect(write_if_missing(path, golden), "seed matte-wash");
      expect(read_file(path) == golden, "matte-wash golden match");
    }
    auto again = rasterize_paper_sheet_shading_reference(16, 12, params);
    expect(again.has_value() && again->rgba == img->rgba, "deterministic");

    PaperSheetShadingParams other = params;
    other.material.seed = 18;
    auto alt = rasterize_paper_sheet_shading_reference(16, 12, other);
    expect(alt.has_value() && alt->rgba != img->rgba, "seed changes output");
  }

  // Scale consistency: 16x12 vs 32x24 within profile tolerance.
  {
    auto small = rasterize_paper_sheet_shading_reference(16, 12, params);
    auto large = rasterize_paper_sheet_shading_reference(32, 24, params);
    expect(small.has_value() && large.has_value(), "scale rasters");
    if (small && large) {
      const Rgba8Image up = upsample_nn(*small, 2);
      const double mean_delta = mean_abs_delta_u8(up, *large);
      expect(mean_delta <= 18.0, "scale mean delta within profile");

      const Rgba8 c_small = at(*small, 8, 6);
      const Rgba8 c_large = at(*large, 16, 12);
      const int center_delta =
          std::abs(static_cast<int>(c_small.r) - static_cast<int>(c_large.r)) +
          std::abs(static_cast<int>(c_small.g) - static_cast<int>(c_large.g)) +
          std::abs(static_cast<int>(c_small.b) - static_cast<int>(c_large.b));
      expect(center_delta / 3 <= 28, "scale center within profile");
      // Quantize mean delta for a stable golden line (profile still uses 18.0).
      const int mean_delta_q = static_cast<int>(std::lround(mean_delta));
      expect(mean_delta_q <= 18, "scale mean delta quantized");

      std::ostringstream ss;
      ss << "name=scale-consistency mean_abs_delta_u8_q=" << mean_delta_q
         << " center_rgb=" << static_cast<int>(c_small.r) << ","
         << static_cast<int>(c_small.g) << "," << static_cast<int>(c_small.b)
         << "->" << static_cast<int>(c_large.r) << ","
         << static_cast<int>(c_large.g) << "," << static_cast<int>(c_large.b)
         << "\n";
      const auto path = golden_dir / "scale-consistency.txt";
      const std::string body = ss.str();
      if (write_goldens) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << body;
        expect(static_cast<bool>(out), "write scale golden");
      } else {
        expect(write_if_missing(path, body), "seed scale golden");
        expect(read_file(path) == body, "scale golden match");
      }
    }
  }

  // Validation edges.
  {
    PaperSheetShadingParams bad;
    bad.material.fiber = 1.5f;
    expect(!validate_paper_sheet_shading_params(bad), "reject fiber");
    bad.material.fiber = 0.2f;
    bad.material.normal_variation = -0.1f;
    expect(!validate_paper_sheet_shading_params(bad), "reject nvar");
    bad.material.normal_variation = 0.1f;
    bad.material.edge_thickness = std::numeric_limits<float>::infinity();
    expect(!validate_paper_sheet_shading_params(bad), "reject edge");
  }

  expect(rasterize_paper_sheet_shading_reference(0, 8, params).has_value() ==
             false,
         "reject zero width");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
