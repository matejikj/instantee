#include <instantee/render_core/print_grain.hpp>

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

[[nodiscard]] std::size_t count_dark(const Rgba8Image &img) {
  std::size_t n = 0;
  for (std::size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
    if (img.rgba[i] < 200 || img.rgba[i + 1] < 200 || img.rgba[i + 2] < 200) {
      ++n;
    }
  }
  return n;
}

[[nodiscard]] std::string golden_dump(std::string_view name,
                                      const PrintGrainParams &params,
                                      const Rgba8Image &img) {
  std::ostringstream ss;
  ss << "name=" << name << " seed=" << params.seed
     << " intensity=" << params.intensity << " reg=" << params.registration_x_px
     << "," << params.registration_y_px << " defects=" << params.defect_density
     << " dark=" << count_dark(img) << '\n';
  const std::uint32_t samples[][2] = {
      {0, 0}, {4, 3}, {8, 6}, {12, 9}, {15, 11}};
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
    std::cerr << "usage: print_grain_test <repo-root> [--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  const auto golden_dir = root / "contracts/fixtures/print-grain/goldens";

  auto registry = make_print_grain_shader_feature_registry();
  expect(registry.contains(kPrintGrainFeature), "grain registered");
  expect(!registry.contains("editorial"), "no editorial style branch");

  auto feature = registry.resolve(kPrintGrainFeature);
  expect(feature.has_value(), "resolve grain");
  if (feature) {
    expect(!feature->params.empty(), "grain params");
    expect(feature->pass_contributions.size() == 1 &&
               feature->pass_contributions[0].pass_key ==
                   kPrintGrainOverlayPass,
           "grain pass");
  }

  MaterialDesc material{.id = "paper_panel",
                        .version = 1,
                        .feature_names = {std::string{kPrintGrainFeature}}};

  // Inactive feature adds no GPU pass.
  auto inactive =
      plan_print_grain_passes(registry, material, std::vector<std::string>{});
  expect(inactive.has_value() && !inactive->grain_active &&
             inactive->pass_keys.empty(),
         "inactive empty pass");

  auto active = plan_print_grain_passes(
      registry, material,
      std::vector<std::string>{std::string{kPrintGrainFeature}});
  expect(active.has_value() && active->grain_active, "active plan");
  if (active) {
    expect(active->pass_keys.size() == 1, "one grain pass");
    expect(active->pass_keys[0] == kPrintGrainOverlayPass, "grain pass key");
  }

  // Seeded overlay golden.
  {
    PrintGrainParams params;
    params.seed = 11;
    params.intensity = 0.22f;
    params.registration_x_px = 1.0f;
    params.registration_y_px = -0.75f;
    params.defect_density = 0.05f;
    auto img = rasterize_print_grain_reference(16, 12, params);
    expect(img.has_value(), "raster grain");
    if (img) {
      expect(count_dark(*img) > 0, "grain darkens paper");
      const std::string golden = golden_dump("grain-overlay", params, *img);
      const auto path = golden_dir / "grain-overlay.txt";
      if (write_goldens) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << golden;
        expect(static_cast<bool>(out), "write grain golden");
      } else {
        expect(write_if_missing(path, golden), "seed grain");
        expect(read_file(path) == golden, "grain golden match");
      }
      auto again = rasterize_print_grain_reference(16, 12, params);
      expect(again.has_value() && again->rgba == img->rgba, "deterministic");

      PrintGrainParams other = params;
      other.seed = 12;
      auto alt = rasterize_print_grain_reference(16, 12, other);
      expect(alt.has_value() && alt->rgba != img->rgba, "seed changes output");
    }
  }

  // Zero intensity still plans a pass when active, but output stays near base.
  {
    PrintGrainParams params;
    params.intensity = 0.0f;
    params.defect_density = 0.0f;
    params.registration_x_px = 0.0f;
    params.registration_y_px = 0.0f;
    auto img = rasterize_print_grain_reference(8, 8, params);
    expect(img.has_value(), "raster zero intensity");
    if (img) {
      // With no grain/defects/registration the center tracks the vignette base.
      const Rgba8 c = at(*img, 4, 4);
      expect(c.r > 200 && c.g > 200 && c.b > 190,
             "near paper when disabled fx");
    }
  }

  // Validation edges.
  {
    PrintGrainParams bad;
    bad.intensity = 1.5f;
    expect(!validate_print_grain_params(bad), "reject intensity");
    bad.intensity = 0.2f;
    bad.defect_density = -0.1f;
    expect(!validate_print_grain_params(bad), "reject density");
    bad.defect_density = 0.1f;
    bad.registration_x_px = std::numeric_limits<float>::infinity();
    expect(!validate_print_grain_params(bad), "reject registration");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
