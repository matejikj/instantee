#include <instantee/render_core/halftone.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

[[nodiscard]] std::size_t count_ink(const Rgba8Image &img) {
  std::size_t n = 0;
  for (std::size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
    if (img.rgba[i] < 128) {
      ++n;
    }
  }
  return n;
}

[[nodiscard]] std::string golden_dump(std::string_view name,
                                      const HalftoneParams &params,
                                      const Rgba8Image &img) {
  std::ostringstream ss;
  ss << "name=" << name << " mode=" << halftone_mode_id(params.mode)
     << " scale=" << params.dot_scale << " res=" << params.resolution_scale
     << " seed=" << params.seed << " samples=" << params.max_samples
     << " mask=" << (params.apply_ellipse_mask ? 1 : 0)
     << " ink=" << count_ink(img) << '\n';
  // Mix of dot centers, known line-ink cells, and frame corners.
  const std::uint32_t samples[][2] = {{2, 2},   {15, 2}, {10, 4},
                                      {16, 12}, {0, 0},  {31, 23}};
  for (const auto &s : samples) {
    const Rgba8 c = at(img, s[0], s[1]);
    ss << "p" << s[0] << "," << s[1] << "=" << static_cast<int>(c.r) << ","
       << static_cast<int>(c.g) << "," << static_cast<int>(c.b) << "\n";
  }
  return ss.str();
}

} // namespace

int main(int argc, char **argv) {
  const bool write_goldens =
      argc == 3 && std::string_view(argv[2]) == "--write-goldens";
  if (argc != 2 && !write_goldens) {
    std::cerr << "usage: halftone_test <repo-root> [--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  const auto golden_dir = root / "contracts/fixtures/halftone/goldens";

  auto registry = make_halftone_shader_feature_registry();
  expect(registry.contains(kHalftoneFeature), "halftone registered");
  expect(registry.contains(kSolidFillFeature), "solid-fill registered");
  expect(!registry.contains("editorial"), "no editorial style branch");

  auto feature = registry.resolve(kHalftoneFeature);
  expect(feature.has_value(), "resolve halftone");
  if (feature) {
    expect(!feature->params.empty(), "halftone params");
    expect(feature->pass_contributions.size() == 1 &&
               feature->pass_contributions[0].pass_key == kHalftoneOverlayPass,
           "halftone pass");
  }

  MaterialDesc material{.id = "photo_panel",
                        .version = 1,
                        .feature_names = {std::string{kSolidFillFeature},
                                          std::string{kHalftoneFeature}}};

  // Profiling contract: inactive feature adds no GPU pass.
  auto inactive =
      plan_halftone_passes(registry, material, std::vector<std::string>{});
  expect(inactive.has_value() && !inactive->halftone_active &&
             inactive->pass_keys.empty(),
         "inactive empty pass");

  auto active = plan_halftone_passes(
      registry, material,
      std::vector<std::string>{std::string{kSolidFillFeature},
                               std::string{kHalftoneFeature}});
  expect(active.has_value() && active->halftone_active, "active plan");
  if (active) {
    expect(active->pass_keys.size() == 2, "solid + halftone passes");
    expect(active->pass_keys[0] == "main_color", "solid first");
    expect(active->pass_keys[1] == kHalftoneOverlayPass, "halftone second");
  }

  // Dot golden on synthetic photo luminance (CPU oracle only).
  {
    HalftoneParams params;
    params.mode = HalftoneMode::dots;
    params.dot_scale = 4.0f;
    params.resolution_scale = 1.0f;
    params.seed = 7;
    params.max_samples = 4;
    auto img = rasterize_halftone_reference(32, 24, params);
    expect(img.has_value(), "raster dots");
    if (img) {
      expect(count_ink(*img) > 0, "dots produce ink");
      const std::string golden = golden_dump("dots-photo", params, *img);
      const auto path = golden_dir / "dots-photo.txt";
      if (write_goldens) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << golden;
        expect(static_cast<bool>(out), "write dots golden");
      } else {
        expect(write_if_missing(path, golden), "seed dots");
        expect(read_file(path) == golden, "dots golden match");
      }
      auto again = rasterize_halftone_reference(32, 24, params);
      expect(again.has_value() && again->rgba == img->rgba, "deterministic");
    }
  }

  // Line mode + ellipse mask (photograph cutout path).
  {
    HalftoneParams params;
    params.mode = HalftoneMode::lines;
    params.dot_scale = 3.0f;
    params.resolution_scale = 0.5f;
    params.seed = 3;
    params.max_samples = 2;
    params.apply_ellipse_mask = true;
    auto img = rasterize_halftone_reference(32, 24, params);
    expect(img.has_value(), "raster lines-mask");
    if (img) {
      // Corner outside ellipse should be paper.
      expect(at(*img, 0, 0).r == 255 && at(*img, 0, 0).g == 255, "mask corner");
      expect(count_ink(*img) > 0, "lines-mask produce ink");
      expect(at(*img, 15, 2).r == 0, "known line ink");
      const std::string golden = golden_dump("lines-mask", params, *img);
      const auto path = golden_dir / "lines-mask.txt";
      if (write_goldens) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << golden;
        expect(static_cast<bool>(out), "write lines golden");
      } else {
        expect(write_if_missing(path, golden), "seed lines");
        expect(read_file(path) == golden, "lines golden match");
      }
    }
  }

  // Sample-count clamp / validation.
  {
    HalftoneParams bad;
    bad.max_samples = 0;
    expect(!validate_halftone_params(bad), "reject zero samples");
    bad.max_samples = 9;
    expect(!validate_halftone_params(bad), "reject high samples");
    bad.max_samples = 4;
    bad.resolution_scale = 1.5f;
    expect(!validate_halftone_params(bad), "reject res scale");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
