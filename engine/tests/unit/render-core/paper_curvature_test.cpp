#include <instantee/render_core/paper_curvature.hpp>
#include <instantee/render_core/shader_features.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__)
#error "render-core tests must remain free of Metal/Apple headers"
#endif

namespace {

using namespace instantee::render_core;
using instantee::core::nearly_equal;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
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

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: paper_curvature_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const std::filesystem::path golden_dir =
      root / "contracts/fixtures/paper-curvature/goldens";

  const auto tier = paper_curvature_tier_lite_v1();
  expect(validate_paper_curvature_tier(tier).has_value(), "tier validates");

  auto mesh = make_paper_curvature_grid(120.0, 80.0, 16, 10, tier);
  expect(mesh.has_value(), "grid mesh");
  if (!mesh) {
    return EXIT_FAILURE;
  }
  expect(validate_paper_curvature_mesh(*mesh, tier).has_value(),
         "mesh validates");

  // Bend: silhouette stays watertight and within tier.
  {
    PaperCurvatureParams params;
    params.mode = PaperCurvatureMode::bend;
    params.bend_angle_rad = 0.8;
    auto pose = evaluate_paper_curvature(*mesh, params, tier);
    expect(pose.has_value(), "bend pose");
    if (pose) {
      auto sil = inspect_paper_curvature_silhouette(*mesh, *pose);
      expect(sil.has_value(), "bend silhouette");
      if (sil) {
        expect(sil->watertight, "bend watertight");
        expect(sil->no_collapsed_edges, "bend edges");
        expect(sil->within_tier, "bend within tier");
        expect(sil->ops_used <= tier.max_total_ops, "ops budget");
      }
      expect(pose->vertices.front().z < 0.0 ||
                 nearly_equal(pose->vertices.front().z, 0.0),
             "bend displaces toward -Z or stays flat at mid");
      // Corners lift for non-zero bend.
      expect(pose->vertices.front().z < -1e-3 ||
                 pose->vertices[mesh->grid_u].z < -1e-3,
             "bend moves sheet ends");
    }
  }

  // Wave: non-zero amp, still watertight.
  {
    PaperCurvatureParams params;
    params.mode = PaperCurvatureMode::wave;
    params.wave_amp_px = 6.0;
    params.wave_freq_u = 2.0;
    auto pose = evaluate_paper_curvature(*mesh, params, tier);
    expect(pose.has_value(), "wave pose");
    if (pose) {
      auto sil = inspect_paper_curvature_silhouette(*mesh, *pose);
      expect(sil && sil->watertight && sil->no_collapsed_edges &&
                 sil->within_tier,
             "wave silhouette ok");
      bool moved = false;
      for (const auto &v : pose->vertices) {
        if (std::fabs(v.z) > 1e-3) {
          moved = true;
          break;
        }
      }
      expect(moved, "wave displaces vertices");
    }
  }

  // Tier enforcement: oversized grid rejected.
  {
    auto huge =
        make_paper_curvature_grid(10.0, 10.0, tier.max_grid_u + 1U, 4, tier);
    expect(!huge.has_value(), "oversize grid rejected");
  }

  // Inactive feature adds no pass; active adds the deform pass key.
  {
    ShaderFeatureRegistry registry;
    expect(register_paper_curvature_feature(registry).has_value(),
           "register feature");
    MaterialDesc material{
        .id = "paper",
        .feature_names = {std::string(kPaperCurvatureFeatureName)}};
    auto inactive = registry.plan_passes(material, {});
    expect(inactive && inactive->pass_keys.empty(), "inactive empty passes");
    auto active = registry.plan_passes(
        material, {std::string(kPaperCurvatureFeatureName)});
    expect(active && active->pass_keys.size() == 1U, "active one pass");
    if (active && !active->pass_keys.empty()) {
      expect(active->pass_keys.front() == "paper_curvature_vertex_deform",
             "pass key");
    }
  }

  // Goldens.
  {
    PaperCurvatureParams bend;
    bend.mode = PaperCurvatureMode::bend;
    bend.bend_angle_rad = 0.8;
    auto pose = evaluate_paper_curvature(*mesh, bend, tier);
    expect(pose.has_value(), "golden bend");
    if (pose) {
      const std::string body =
          format_paper_curvature_golden("bend-0.8", *mesh, *pose, bend);
      const auto path = golden_dir / "bend-0.8.txt";
      expect(write_if_missing(path, body), "write bend golden");
      expect(read_file(path) == body, "bend golden matches");
    }

    PaperCurvatureParams wave;
    wave.mode = PaperCurvatureMode::wave;
    wave.wave_amp_px = 6.0;
    wave.wave_freq_u = 2.0;
    auto wpose = evaluate_paper_curvature(*mesh, wave, tier);
    expect(wpose.has_value(), "golden wave");
    if (wpose) {
      const std::string body =
          format_paper_curvature_golden("wave-amp6", *mesh, *wpose, wave);
      const auto path = golden_dir / "wave-amp6.txt";
      expect(write_if_missing(path, body), "write wave golden");
      expect(read_file(path) == body, "wave golden matches");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
